/**
 * @file
 * @brief Mutex for stateful tasks. Declaration of class AIStatefulTaskMutex.
 *
 * @Copyright (C) 2019  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Must be included first.
#include "AIStatefulTask.h"

#ifndef AISTATEFULTASKMUTEX_H
#define AISTATEFULTASKMUTEX_H

#include "threadsafe/aithreadsafe.h"
#include "utils/NodeMemoryResource.h"
#include "utils/cpu_relax.h"
#include "debug.h"

//
// A task mutex.
//
// Prevent different tasks from concurrently entering the same critical area.
//
// Consider an object that is shared between tasks, but may
// not be simultaneously accessed by two different threads.
//
// For example,
//
//
//   struct Shared
//   {
//     AIStatefulTaskMutex m_task_mutex;
//     void do_work();    // Only one thread at a time may call this.
//
//     Shared(utils::NodeMemoryResource& node_memory_resource) : m_task_mutex(node_memory_resource) { }
//   };
//
//   utils::MemoryPagePool mpp(0x8000);
//   utils::NodeMemoryResource node_memory_resource(mpp);
//   Shared shared(node_memory_resource);
//
// Then multiple running tasks could use this to prevent concurrent access:
//
// ...
//   case MyTask_lock:
//     set_state(MyTask_locked);
//     if (!shared.m_task_mutex.lock(this, 1))
//     {
//       wait(1);
//       break;
//     }
//     [[fallthrough]];
//   case MyTask_locked:
//     shared.do_work();
//     shared.m_task_mutex.unlock();
//
class AIStatefulTaskMutex
{
 protected:
  struct Node
  {
    std::atomic<Node*> m_next;
    AIStatefulTask* m_task;
    AIStatefulTask::condition_type m_condition;
  };

 public:
  // Returns the size of the nodes that will be allocated from m_node_memory_resource.
  static constexpr size_t node_size() { return sizeof(Node); }

 private:
  utils::NodeMemoryResource& m_node_memory_resource;    // Reference to memory resource to allocate Node's from.
  std::atomic<Node*> m_head;                            // The mutex is locked when this atomic has value nullptr.
  std::atomic<Node*> m_owner;                           // After locking this mutex, the owner sets this pointer to point to
                                                        // its Node (m_owner->m_task will point to the owning task).
 public:
  // Construct an unlocked AIStatefulTaskMutex.
  AIStatefulTaskMutex(utils::NodeMemoryResource& node_memory_resource) : m_node_memory_resource(node_memory_resource), m_head(nullptr), m_owner(nullptr) { }
  // Immediately after construction, nobody owns the lock:
  //
  // m_head --> nullptr

  // Try to obtain ownership for owner (recursive locking allowed).
  // Returns true upon success and false upon failure to obtain ownership.
  bool lock(AIStatefulTask* task, AIStatefulTask::condition_type condition)
  {
    DoutEntering(dc::notice, "AIStatefulTaskMutex::lock(" << task << ", " << condition << ") [" << this << "]");

    Node* new_node = new (m_node_memory_resource.allocate(sizeof(Node))) Node;
    std::atomic_init(&new_node->m_next, static_cast<Node*>(nullptr));
    new_node->m_task = task;

//    Dout(dc::notice, "Create new node at " << new_node << " with m_next = " << new_node->m_next << "; m_task = " << new_node->m_task << " [" << task << "]");

    Node* prev = m_head.exchange(new_node, std::memory_order_acq_rel);
//    Dout(dc::notice, "Replaced m_head with " << new_node << ", previous value of m_head was " << prev << " [" << task << "]");
    //            new_node:
    // m_head -->.----------------.
    //           | m_next --------+--> nullptr
    //           | m_task -> task |
    //           `----------------'
    //
    // If the previous value of m_head (prev) equal nullptr then we were
    // the first and own the lock.
    if (prev == nullptr)
    {
      Dout(dc::notice, "Mutex acquired, setting m_owner to " << new_node << " [" << task << "]");
      m_owner.store(new_node, std::memory_order_release);
      return true;
    }
    Dout(dc::notice, "Failed to acquire mutex [" << task << "]");

    // If prev is non-null then another task owned the lock at the moment
    // we executed the above exchange. This task, when executing unlock(),
    // will fail the compare_exchange_strong because we just changed the
    // value of m_head.
    //
    // Moreover that task then will spin lock on reading m_owner->m_next
    // because that was initialized with nullptr when that task entered
    // lock() and never changed.
    //
    // Below we set that m_next pointer to point to our node, so the
    // task in unlock() can escape the spin lock.
//    Dout(dc::notice, "Setting m_condition of new_node (" << new_node << ") to " << condition << " [" << task << "]");
    new_node->m_condition = condition;
//    Dout(dc::notice, "Setting m_next of prev (" << prev << ") to " << new_node << " [" << task << "]");
    prev->m_next.store(new_node, std::memory_order_release);

    // Obtaining the lock failed. Halt the task.
    //task->wait(condition);
    return false;
  }

  // Undo one (succcessful) call to lock.
  void unlock()
  {
    DoutEntering(dc::notice, "AIStatefulTaskMutex::unlock() [" << this << "]");

    Node* const owner = m_owner.load(std::memory_order_relaxed);
#ifdef CWDEBUG
AIStatefulTask* const task = owner->m_task;
#endif
//    Dout(dc::notice, "Setting m_owner to nullptr, was " << owner << " [" << task << "]");
    m_owner.store(nullptr, std::memory_order_relaxed);
//    Dout(dc::notice, "Calling m_head.compare_exchange_strong(owner = " << owner << ", nullptr)");
    Node* expected = owner;
    if (m_head.compare_exchange_strong(expected, nullptr, std::memory_order_release, std::memory_order_relaxed))
    {
      Dout(dc::notice, "Success - m_head was reset to nullptr [" << task << "]");
//      Dout(dc::notice, "deallocating " << owner << " [" << task << "]");
      m_node_memory_resource.deallocate(owner);
//      Dout(dc::notice, "Leaving unlock()");
      return;
    }
//    Dout(dc::notice, "m_head not changed it wasn't equal to owner (" << owner << "), m_head is " << expected << " [" << task << "]");
    // Wait until the task that tried to get the lock first (after us) set m_next.
    Node* next;
//next = owner->m_next.load(std::memory_order_acquire);
//    Dout(dc::notice, "owner->m_next = " << next << " [" << task << "]");
    while (!(next = owner->m_next.load(std::memory_order_acquire)))
      cpu_relax();
//    Dout(dc::notice, "deallocating " << owner << " [" << task << "]");
    m_node_memory_resource.deallocate(owner);
    Dout(dc::notice, "Setting m_owner to " << next << " [" << task << "]");
    m_owner.store(next, std::memory_order_release);
    Dout(dc::notice, "Calling signal(" << next->m_condition << ") on next->m_task (" << next->m_task << ") with next = " << next << " [" << task << "]");
    next->m_task->signal(next->m_condition);
//    Dout(dc::notice, "Leaving unlock()");
  }

 private:
  friend class AIStatefulTask;
  // Is this object currently owned (locked) by us?
  // May only be called from some multiplex_impl passing its own AIStatefulTask pointer,
  // and therefore only by AIStatefulTask::is_self_locked(AIStatefulTaskMutex&).
  //
  //   case SomeState:
  //     ...
  //     if (is_self_locked(the_mutex))
  //      ...
  bool is_self_locked(AIStatefulTask const* caller) const
  {
    Node* owner = m_owner.load(std::memory_order_acquire);
    return owner && owner->m_task == caller;
  }
};

#endif // AISTATEFULTASKMUTEX_H
