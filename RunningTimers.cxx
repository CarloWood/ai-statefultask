#include "sys.h"
#include "RunningTimers.h"
#include "AIThreadPool.h"
#include "utils/macros.h"
#include <new>

extern "C" void sigalrm_handler(int)
{
  Dout(dc::notice, "Calling sigalrm_handler()");
}

namespace statefultask {

RunningTimers::RunningTimers()
{
  // Initialize m_cache and m_tree.
  for (int interval = 0; interval < tree_size; ++interval)
  {
    m_cache[interval] = Timer::s_none;
    int parent_ti = interval_to_parent_index(interval);
    m_tree[parent_ti] = interval & ~1;
  }
  // Initialize the rest of m_tree.
  for (int index = tree_size / 2 - 1; index > 0; --index)
    m_tree[index] = m_tree[left_child_of(index)];

  // Call sigalrm_handler when the SIGALRM signal is caught by a thread.
  struct sigaction action;
  std::memset(&action, 0, sizeof(struct sigaction));
  action.sa_handler = sigalrm_handler; 
  if (sigaction(SIGALRM, &action, NULL) == -1) 
    assert(false);
  // Block the SIGALRM signal.
  sigemptyset(&m_blocked_signals);
  sigaddset(&m_blocked_signals, SIGALRM);
  sigprocmask(SIG_BLOCK, &m_blocked_signals, nullptr);
}

RunningTimers::Current::Current() : timer(nullptr), need_update(false)
{
  // Create a monotonic timer.
  timer_create(CLOCK_MONOTONIC, nullptr, &posix_timer);
}

Timer::Handle RunningTimers::push(TimerQueueIndex interval, Timer* timer)
{
  assert(interval.get_value() < m_queues.size());
  uint64_t sequence;
  bool is_current;
  Timer::time_point expiration_point = timer->get_expiration_point();
  {
    timer_queue_t::wat queue_w(m_queues[interval]);
    sequence = queue_w->push(timer);
    is_current = queue_w->is_current(sequence);
    // Being 'current' means that it is the first timer in this queue.
    // Since the Handle for this timer isn't returned yet, it can't be
    // cancelled by another thread, so it will remain the current timer
    // until we leave this function. It is not necessary to keep the
    // queue locked.
    if (is_current)
      m_mutex.lock();
  }
  Timer::Handle handle{interval, sequence};
  if (is_current)
  {
    int cache_index = to_cache_index(interval);
    decrease_cache(cache_index, expiration_point);
    is_current = m_tree[1] == cache_index;
    m_mutex.unlock();
    if (is_current)
    {
      update_running_timer();
      current_t::wat current_w{m_current};
      if (!current_w->need_update && !current_w->timer)
      {
        Dout(dc::notice, "3. need_update = true");
        current_w->need_update = true;
        AIThreadPool::instance().notify_one();
      }
    }
  }
  return handle;
}

Timer* RunningTimers::update_current_timer(current_t::wat& current_w, Timer::time_point now)
{
  DoutEntering(dc::notice, "RunningTimers::update_current_timer(current_w, " << now.time_since_epoch().count() << ")");

  // Don't call this function while we have a current timer or when need_update isn't set.
  ASSERT(current_w->need_update && current_w->timer == nullptr);
  bool need_update_cleared_and_current_unlocked = false;

  // Initialize interval, next and timer to correspond to the Timer in RunningTimers
  // that is the first to expire next, if any (if not then return nullptr).
  int interval;
  Timer::time_point next;
  Timer* timer;
  Timer::time_point::duration duration;
  m_mutex.lock();
  while (true)  // So we can use continue.
  {
    interval = m_tree[1];                     // The interval of the timer that will expire next.
    next = m_cache[interval];                 // The time at which it will expire.

    if (next == Timer::s_none)                // Is there a next timer at all?
    {
      m_mutex.unlock();
      if (AI_UNLIKELY(need_update_cleared_and_current_unlocked))
        current_w.relock(m_current);          // Lock m_current again.
      Dout(dc::notice, "1. need_update = false");
      current_w->need_update = false;         // Block subsequent calls to update_current_timer.
      // current_w->need_update and current_w->timer are unset.
      Dout(dc::notice, "No timers.");
      return nullptr;                         // There is no next timer.
    }

    if (AI_LIKELY(!need_update_cleared_and_current_unlocked))
    {
      Dout(dc::notice, "2. need_update = false");
      current_w->need_update = false;         // Block subsequent calls to update_current_timer.
      current_w.unlock();                     // Unlock m_current.
      need_update_cleared_and_current_unlocked = true;
    }

    m_mutex.unlock();                         // The queue must be locked first in order to avoid a deadlock.
    timer_queue_t::wat queue_w(m_queues[to_queues_index(interval)]);
    m_mutex.lock();                           // Lock m_mutex again.
    // Because of the short unlock of m_mutex, m_tree[1] and/or m_cache[interval] might have changed.
    next = m_cache[interval];
    if (AI_UNLIKELY(m_tree[1] != interval || next == Timer::s_none))    // Was there a race? Then try again.
      continue;
    duration = next - now;
    if (duration.count() <= 0)                // Did this timer already expire?
    {
      Dout(dc::notice, "Timer " << (void*)timer << " expired " << -std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() << " ns ago.");
      m_mutex.unlock();
      timer = queue_w->pop(m_mutex);
      increase_cache(interval, queue_w->next_expiration_point());
      m_mutex.unlock();
      current_w.relock(m_current);            // Lock m_current again.
      Dout(dc::notice, "1. need_update = true");
      current_w->need_update = true;          // Allow calls to update_current_timer again.
      // current_w->need_update is true and current_w->timer is unset.
      Dout(dc::notice, "Expired timer.");
      return timer;                           // Do the call back.
    }
    Dout(dc::notice, "Timer did not expire yet.");
    timer = queue_w->peek();
    break;
  }
  m_mutex.unlock();

  // Calculate the timespec at which the current timer will expire.
  struct itimerspec new_value;
  memset(&new_value.it_interval, 0, sizeof(struct timespec));
  // This rounds down since duration is positive.
  auto s = std::chrono::duration_cast<std::chrono::seconds>(duration);
  new_value.it_value.tv_sec = s.count();
  auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - s);
  new_value.it_value.tv_nsec = ns.count();

  // Update the POSIX timer.
  Dout(dc::notice, "Calling timer_settime() for " << new_value.it_value.tv_sec << " seconds and " << new_value.it_value.tv_nsec << " nanoseconds.");
  if (AI_UNLIKELY(sigprocmask(SIG_BLOCK, nullptr, &m_blocked_signals) == -1))
    assert(false);
  // SIGALRM should already be blocked when we get here. Don't unblock SIGALRM anywhere except in wait_for_signals().
  ASSERT(sigismember(&m_blocked_signals, SIGALRM));
  current_w.relock(m_current);                  // Lock m_current again.
  bool pending = timer_settime(current_w->posix_timer, 0, &new_value, nullptr) == 0;
  ASSERT(pending);
  current_w->timer = timer;

  // Return nullptr, but this time with current_w->need_update false and current_w->timer set.
  Dout(dc::notice, "Timer started.");
  return nullptr;
}

void RunningTimers::wait_for_signals()
{
  Dout(dc::notice, "Calling sigsuspend.");
  sigdelset(&m_blocked_signals, SIGALRM);               // Wait for SIGALRM.
  sigsuspend(&m_blocked_signals);
  ASSERT(errno == EINTR);
  Dout(dc::notice, "Returning from sigsuspend.");
}

RunningTimers::~RunningTimers()
{
  DoutEntering(dc::notice, "RunningTimers::~RunningTimers() with m_queues.size() == " << m_queues.size());
  // Set all timers to 'not running', otherwise they call cancel() on us when they're being destructed.
  for (TimerQueueIndex interval = m_queues.ibegin(); interval != m_queues.iend(); ++interval)
    timer_queue_t::wat(m_queues[interval])->set_not_running();
}

} // namespace statefultask

namespace {
  SingletonInstance<statefultask::RunningTimers> dummy __attribute__ ((__unused__));
} // namespace
