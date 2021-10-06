#pragma once

#include "AIStatefulTask.h"
#include "statefultask/DefaultMemoryPagePool.h"
#include "utils/DequeAllocator.h"
#include <deque>
#include "debug.h"

namespace statefultask {

// TaskEvent
//
// Usage:
//
// Task A has a public member of type TaskEvent and calls
// trigger() on it when the event occurs. For example,
//
//   TaskEvent m_foo_available;
//   Foo get_foo();             // Only call this when foo is available.
//
// Other tasks have a state that does:
//
//  case WaitForFoo:
//     task_a->m_foo_available.register_task(this, foo_available_conditon);
//     set_state(FooAvailable);
//     wait(foo_available_conditon);
//     break;
//   case FooAvailable:
//   {
//     Foo foo = task_a->get_foo();
//
class TaskEvent
{
 private:
  using data_type = std::pair<boost::intrusive_ptr<AIStatefulTask>, AIStatefulTask::condition_type>;
  using container_type = std::deque<data_type, utils::DequeAllocator<data_type>>;
  using registered_tasks_t = aithreadsafe::Wrapper<container_type, aithreadsafe::policy::Primitive<std::mutex>>;

  mutable utils::NodeMemoryResource m_nmr{AIMemoryPagePool::instance()};
  mutable registered_tasks_t m_registered_tasks{utils::DequeAllocator<data_type>(m_nmr)};
  std::atomic_bool m_triggered = false;

  void trigger(container_type const& waiting_tasks) const
  {
    for (auto& p : waiting_tasks)
      p.first->signal(p.second);
  }

 public:
  // Call task->signal(condition) if trigger() was already called, otherwise
  // keep task alive and call signal when trigger is called.
  // Not really "const" because it alters m_registered_tasks and m_nmr, but this way is
  // more convenient: now we can call m_other_task->some_event.register_task(this, my_condition)
  // from a task where m_other_task is pointer to (otherwise) const.
  void register_task(AIStatefulTask* task, AIStatefulTask::condition_type condition) const
  {
    using std::memory_order;
    if (m_triggered.load(memory_order::relaxed))
      task->signal(condition);
    else
    {
      container_type* waiting_tasks;
      bool need_trigger = false;
      {
        registered_tasks_t::wat registered_tasks_w(m_registered_tasks);
        registered_tasks_w->emplace_back(task, condition);
        // In the unlikely case that the event was triggered between reading
        // m_triggered at the top of this function and locking m_registered_tasks:
        if (AI_UNLIKELY(m_triggered.load(memory_order::relaxed)))
        {
          waiting_tasks = new container_type(utils::DequeAllocator<data_type>(m_nmr));
          waiting_tasks->swap(*registered_tasks_w);
          need_trigger = true;
        }
      } // Unlock m_registered_tasks.
      if (AI_UNLIKELY(need_trigger))
      {
        trigger(*waiting_tasks);
        delete waiting_tasks;
      }
    }
  }

  // Mark that this event was triggered and call signal on the tasks that already
  // called register_task.
  void trigger()
  {
    using std::memory_order;
    // It makes no sense to trigger it twice.
    ASSERT(!m_triggered.load(memory_order::relaxed));
    m_triggered.store(true, memory_order::relaxed);
    // Move the deque to a local variable, so that we don't keep the lock on m_registered_tasks while calling the signal()'s.
    container_type waiting_tasks(std::move(*registered_tasks_t::rat(m_registered_tasks)));
    trigger(waiting_tasks);
  }
};

} // namespace statefultask
