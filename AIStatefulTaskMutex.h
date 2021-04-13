/**
 * ai-statefultask -- Asynchronous, Stateful Task Scheduler library.
 *
 * @file
 * @brief Mutex for stateful tasks. Declaration of class AIStatefulTaskMutex.
 *
 * @Copyright (C) 2019  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This file is part of ai-statefultask.
 *
 * Ai-statefultask is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Ai-statefultask is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with ai-statefultask.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "threadsafe/aithreadsafe.h"
#include "utils/NodeMemoryResource.h"
#include "utils/cpu_relax.h"
#include "debug.h"

class AIStatefulTask;

/**
 * A task mutex.
 *
 * Prevent different tasks from concurrently entering the same critical area.
 *
 * Consider an object that is shared between tasks, but may
 * not be simultaneously accessed by two different threads.
 *
 * For example,
 *
 *   // Required memory management.
 *   utils::MemoryPagePool mpp(0x8000);
 *   // A task mutex.
 *   AIStatefulTaskMutex m;
 *
 * Then multiple running tasks could use this to prevent concurrent access:
 *
 * ...
 *   case MyTask_lock:
 *     set_state(MyTask_locked);
 *     if (!m.lock(this, 1))
 *     {
 *       wait(1);
 *       break;
 *     }
 *     [[fallthrough]];
 *   case MyTask_locked:
 *     do_work();
 *     m.unlock();
 */
class AIStatefulTaskMutex
{
  using condition_type = uint32_t;      // Must be the same as AIStatefulTask::condition_type

 private:
  // Node in a singly linked list of tasks that are waiting for this mutex.
  struct Node
  {
    std::atomic<Node*> m_next;
    AIStatefulTask* m_task;
    condition_type const m_condition;

    Node(AIStatefulTask* task, condition_type condition) : m_next(nullptr), m_task(task), m_condition(condition) { }
  };

 public:
  /// Returns the size of the nodes that will be allocated from s_node_memory_resource.
  static constexpr size_t node_size() { return sizeof(Node); }
  /// This must be called once before using a AIStatefulTaskMutex.
  static void init(utils::MemoryPagePool* mpp_ptr) { s_node_memory_resource.init(mpp_ptr, node_size()); }
  static utils::NodeMemoryResource s_node_memory_resource;      ///< Memory resource to allocate Node's from.

 private:
  std::atomic<Node*> m_head;                            // The mutex is locked when this atomic has a non-nullptr value.
  std::atomic<Node*> m_owner;                           // After locking this mutex, the owner sets this pointer to point to
                                                        // its Node (m_owner->m_task will point to the owning task).
 public:
  /// Construct an unlocked AIStatefulTaskMutex.
  AIStatefulTaskMutex() : m_head(nullptr), m_owner(nullptr) { }
  // Immediately after construction, nobody owns the lock:
  //
  // m_head --> nullptr

  /// Try to obtain ownership for owner (recursive locking allowed).
  ///
  /// @returns True upon success and false upon failure to obtain ownership.
  bool lock(AIStatefulTask* task, condition_type condition)
  {
    DoutEntering(dc::notice, "AIStatefulTaskMutex::lock(" << task << ", " << condition << ") [" << this << "]");

    Node* new_node = new (s_node_memory_resource.allocate(sizeof(Node))) Node(task, condition);

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
    Dout(dc::notice, "Mutex already locked by [" << task << "]");

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
//    Dout(dc::notice, "Setting m_next of prev (" << prev << ") to " << new_node << " [" << task << "]");
    prev->m_next.store(new_node, std::memory_order_release);

    // Obtaining the lock failed. Halt the task
    return false;       // The caller must call task->wait(condition).
  }

  /// Undo one (succcessful) call to lock.
  void unlock()
  {
    DoutEntering(dc::notice, "AIStatefulTaskMutex::unlock() [" << this << "]");

    Node* const owner = m_owner.load(std::memory_order_relaxed);
#ifdef CWDEBUG
    // Note: this task might already have been destructed!
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
      s_node_memory_resource.deallocate(owner);
//      Dout(dc::notice, "Leaving unlock()");
      return;
    }

    signal_next(owner COMMA_CWDEBUG_ONLY(task));
  }

#if defined(CWDEBUG) && !defined(DOXYGEN)
  // The returned value might point to a task that was already destructed.
  AIStatefulTask* debug_get_owner() const
  {
    // Very racy, not-thread-safe code for debugging output only.
    Node* owner;
    if (m_head.load(std::memory_order_relaxed) == nullptr || (owner = m_owner.load(std::memory_order_relaxed)) == nullptr)
      return nullptr;
    return owner->m_task;
  }
#endif

 private:
  void signal_next(Node* const owner COMMA_CWDEBUG_ONLY(AIStatefulTask* const task));

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
