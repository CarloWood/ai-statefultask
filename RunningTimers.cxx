#include "sys.h"
#include "RunningTimers.h"

namespace statefultask {

RunningTimers::RunningTimers()
{
  for (int interval = 0; interval < tree_size; ++interval)
  {
    m_cache[interval] = Timer::none;
    int parent_ti = interval_to_parent_index(interval);
    m_tree[parent_ti] = interval & ~1;
  }
  for (int index = tree_size / 2 - 1; index > 0; --index)
  {
    m_tree[index] = m_tree[left_child_of(index)];
  }
}

Timer::time_point::duration RunningTimers::expire_next(Timer::time_point now)
{
  Timer::time_point::duration duration;
  while (true)
  {
    int interval = m_tree[1];                   // The interval of the timer that will expire next.
    Timer::time_point next = m_cache[interval]; // The time at which it will expire.
    duration = next - now;                      // How long it takes for the next timer to expire.

    if (duration.count() > 0)
      break;

    // Pop the timer from the queue.
    statefultask::TimerQueue& queue{m_queues[to_queues_index(interval)]};
    Timer* timer = queue.pop();
    // Update m_cache.
    increase_cache(interval, queue.next_expiration_point());
    // Do the call back.
    timer->expire();
  }
  return duration;
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
