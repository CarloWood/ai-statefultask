#pragma once

#include "AIStatefulTask.h"
#include "threadsafe/PointerStorage.h"
#include <limits>
#include "debug.h"

namespace statefultask {

// A class to keep track of tasks, providing a means of mass abortion.
//
// Task can add themselves from initialize_impl, and then must remove
// themselves again in finish_impl.
//
// The call to abort_all will block tasks that call add(), make a copy
// of all currently added tasks (keeping them alive with boost::intrusive_ptr),
// unblock tasks that call add() (but now aborting them instead of adding
// them) and then call abort() on all the copied tasks.
//
class RunningTasksTracker
{
 public:
  using index_type = aithreadsafe::VoidPointerStorage::index_type;
  static constexpr index_type s_aborted = std::numeric_limits<index_type>::max();

 private:
  aithreadsafe::PointerStorage<AIStatefulTask> m_tasks;

 public:
  RunningTasksTracker(uint32_t initial_size) : m_tasks(initial_size) { }

  index_type add(AIStatefulTask* task)
  {
    if (AI_UNLIKELY(m_aborted))
    {
      task->abort();
      return s_aborted;
    }
    return m_tasks.insert(task);
  }

  void remove(index_type index)
  {
    // Is it avoidable to call this when the task was aborted before it was added?
    ASSERT(index != s_aborted);
    m_tasks.erase(index);
  }

  void abort_all();
  bool m_aborted{false};
};

} // namespace statefultask
