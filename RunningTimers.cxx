#include "sys.h"
#include "RunningTimers.h"
#include "utils/macros.h"

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
}

RunningTimers::Current::Current() : timer(nullptr)
{
  // Prepare a sigset_t that has only the SIGALRM bit set.
  sigemptyset(&sigalrm_set);
  sigaddset(&sigalrm_set, SIGALRM);
  // Block the SIGALRM signal.
  sigprocmask(SIG_BLOCK, &sigalrm_set, nullptr);

  // Create a monotonic timer.
  timer_create(CLOCK_MONOTONIC, nullptr, &posix_timer);
}

// Check if there is a next timer,
//   if so, check if it is expired,
//     if so, return a pointer to the expired timer.
//     Otherwise set the new expiration point and return nullptr.
//   If there are no running timers, return nullptr.
//
// This function must be thread-safe.
Timer* RunningTimers::next_expired(current_t::wat const& current_w, Timer::time_point now)
{
  // Don't call this function while we have a current timer.
  ASSERT(current_w->timer == nullptr);

  Timer::time_point next;

  // Move the current timer from m_queues to m_current.
  {
    std::unique_lock<std::mutex> lk(m_mutex);

    int interval = m_tree[1];                   // The interval of the timer that will expire next.
    next = m_cache[interval];                   // The time at which it will expire.

    if (next == Timer::s_none)                  // Is there a next timer at all?
      return nullptr;                           // No, return null.

    statefultask::TimerQueue& queue{m_queues[to_queues_index(interval)]};
    current_w->timer = queue.pop();
    increase_cache(interval, queue.next_expiration_point());      // Update m_cache.
  }

  Timer::time_point::duration duration = next - now;
  if (duration.count() <= 0)                    // Did this timer already expire?
  {
    Timer* timer = current_w->timer;
    current_w->timer = nullptr;                 // Allow calls to next_expired again.
    return timer;                               // Do the call back.
  }

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
#ifdef CWDEBUG
  // Signals should be blocked when we get here.
  static sigset_t blocked_signals;
  if (AI_UNLIKELY(sigprocmask(SIG_BLOCK, nullptr, &blocked_signals) == -1))
    assert(false);
  ASSERT(sigismember(&blocked_signals, SIGALRM));
#endif
  if (AI_UNLIKELY(timer_settime(current_w->posix_timer, 0, &new_value, nullptr) == -1))
    assert(false);

  return nullptr;
}

RunningTimers::~RunningTimers()
{
  DoutEntering(dc::notice, "RunningTimers::~RunningTimers() with m_queues.size() == " << m_queues.size());
  // Set all timers to 'not running', otherwise they call cancel() on us when they're being destructed.
  for (TimerQueueIndex interval = m_queues.ibegin(); interval != m_queues.iend(); ++interval)
    m_queues[interval].set_not_running();
}

} // namespace statefultask

namespace {
  SingletonInstance<statefultask::RunningTimers> dummy __attribute__ ((__unused__));
} // namespace
