#include "sys.h"
#include "RunningTimers.h"
#include "utils/macros.h"

extern "C" void sigalrm_handler(int)
{
  Dout(dc::notice, "Calling sigalarm_handler()");
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

  // Prepare a sigset_t that has only the SIGALRM bit set.
  sigemptyset(&m_sigalrm_set);
  sigaddset(&m_sigalrm_set, SIGALRM);
  // Block the SIGALRM signal.
  sigprocmask(SIG_BLOCK, &m_sigalrm_set, nullptr);

  // Create a monotonic timer.
  timer_create(CLOCK_MONOTONIC, nullptr, &m_timer);
}

bool RunningTimers::expire_next(Timer::time_point now)
{
  // Block the SIGALRM signal so that it will be pending instead of being handled
  // immediately when the timer expires before we're actively waiting for it.
  if (AI_UNLIKELY(sigprocmask(SIG_BLOCK, &m_sigalrm_set, &m_prev_sigset) == -1))
    assert(false);

  Timer::time_point::duration duration;
  while (true)
  {
    int interval = m_tree[1];                   // The interval of the timer that will expire next.
    Timer::time_point next = m_cache[interval]; // The time at which it will expire.
    duration = next - now;

    if (duration.count() > 0)
    {
      if (next == Timer::s_none)
        return false;
      break;
    }

    // Pop the timer from the queue.
    statefultask::TimerQueue& queue{m_queues[to_queues_index(interval)]};
    Timer* timer = queue.pop();
    // Update m_cache.
    increase_cache(interval, queue.next_expiration_point());
    // Do the call back.
    timer->expire();
  }

  struct itimerspec new_value;
  memset(&new_value.it_interval, 0, sizeof(struct timespec));
  // This rounds down since duration is positive.
  auto s = std::chrono::duration_cast<std::chrono::seconds>(duration);
  new_value.it_value.tv_sec = s.count();
  auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - s);
  new_value.it_value.tv_nsec = ns.count();
  Dout(dc::notice, "Calling timer_settime() for " << new_value.it_value.tv_sec << " seconds and " << new_value.it_value.tv_nsec << " nanoseconds.");
  if (AI_UNLIKELY(timer_settime(m_timer, 0, &new_value, nullptr) == -1))
    assert(false);

  // A timer was set.
  return true;
}

RunningTimers::~RunningTimers()
{
  // Set all timers to 'not running', otherwise they call cancel() on us when they're being destructed.
  for (TimerQueueIndex interval = m_queues.ibegin(); interval != m_queues.iend(); ++interval)
    m_queues[interval].set_not_running();
}

} // namespace statefultask

namespace {
  SingletonInstance<statefultask::RunningTimers> dummy __attribute__ ((__unused__));
} // namespace
