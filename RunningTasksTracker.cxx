#include "sys.h"
#include "RunningTasksTracker.h"

namespace statefultask {

void RunningTasksTracker::abort_all()
{
  // This object is for one-time use. Only call RunningTasksTracker::abort_all once.
  ASSERT(!m_aborted);
  // Keep running tasks alive with a boost::intrusive_ptr reference count.
  std::vector<boost::intrusive_ptr<AIStatefulTask>> running_tasks;
  // Cause new calls to add() to abort the task instead of adding them.
  m_aborted = true;
  // Block calls to m_tasks.insert (and erase), copy the running tasks to our local vector, and unblock m_tasks again.
  m_tasks.for_each([&running_tasks](AIStatefulTask* task_ptr){ running_tasks.emplace_back(task_ptr); });
  // Call abort on the copied tasks.
  for (auto&& ptr : running_tasks)
    ptr->abort();
}

} // namespace statefultask
