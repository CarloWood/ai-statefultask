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

void RunningTimers::expire_next()
{
  int const interval = m_tree[1];                             // The interval of the timer that will expire next.
  statefultask::TimerQueue& queue{m_queues[to_queues_index(interval)]};

  Timer* timer = queue.pop();

  // Execute the algorithm for cache value becoming greater.
  increase_cache(interval, queue.next_expiration_point());

  timer->expire();
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
