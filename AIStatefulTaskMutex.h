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

#ifndef AISTATEFULTASKMUTEX_H
#define AISTATEFULTASKMUTEX_H

#include "threadsafe/threadsafe.h"
#include "utils/NodeMemoryResource.h"
#include "utils/threading/MpscQueue.h"
#include "utils/FuzzyBool.h"
#include "utils/cpu_relax.h"
#include "debug.h"

class AIStatefulTask;

// Node in a singly linked list of tasks that are waiting for this mutex.
struct AIStatefulTaskMutexNode : public utils::threading::MpscNode
{
  using condition_type = uint32_t;      // Must be the same as AIStatefulTask::condition_type

  AIStatefulTask* m_task;
  condition_type const m_condition;

  AIStatefulTaskMutexNode(AIStatefulTask* task, condition_type condition) : m_task(task), m_condition(condition) { }
};

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
 *   case MyTask_wait_for_lock:
 *     set_state(MyTask_locked);
 *     if (!m.lock(this, 1))
 *     {
 *       wait(1);
 *       break;
 *     }
 *     [[fallthrough]];
 *   case MyTask_locked:
 *   {
 *     statefultask::AdoptLock lock(m);
 *     do_work();
 *     lock.unlock();   // Optional
 *     ... code that does not require the lock...
 *   }
 */
class AIStatefulTaskMutex
{
  using condition_type = uint32_t;      // Must be the same as AIStatefulTask::condition_type
  using Node = AIStatefulTaskMutexNode;

 private:
  class Queue : public utils::threading::MpscQueue
  {
    using MpscNode = utils::threading::MpscNode;
    std::mutex m_abort_mutex;

   public:
    utils::FuzzyBool push(MpscNode* node)
    {
      node->m_next.store(nullptr, std::memory_order_relaxed);
      MpscNode* prev = m_head.exchange(node, std::memory_order_relaxed);
      // The only way node is guaranteed the next node that will be popped is when
      // we just pushed to an empty list. In that case the situation right now is:
      //
      //   m_tail --> m_stub ==> nullptr, node
      //                ^
      //                |
      //              prev
      //
      // This is a stable situation: calls to push by other threads just append
      // on the right (change m_head), and calls to pop shouldn't happen (we
      // are the owner of the lock; but even if it would happened, then pop will
      // return nullptr and do nothing).
      //
      // If prev is not pointing to m_stub, then the situation is instead:
      //
      //   m_tail --> node1 ==> nullptr, node
      //                ^
      //                |
      //              prev
      //
      // Possibly with more nodes (including m_stub) on the left, possibly incompletely
      // linked, that can all be removed by calls to pop until we get this situation.
      // That is also a stable situation as a call to pop in this case will return nullptr,
      // because m_head is guaranteed unequal to m_tail.
      //
      // So in that case we are certain that at least one more pop() will finish
      // before our node becomes the front node and we can return WasFalse.
      //
      // Assuming that prev does point to m_stub but we are not (yet) the next node
      // that would be popped, for example, the situation is:
      //
      //   m_tail --> node1 ==> m_stub ==> nullptr, node
      //                           ^
      //                           |
      //                         prev
      //
      // then pop will return node1 and set m_tail to point to m_stub to get
      // the first situation. If we see that m_tail points to a node that is not
      // m_stub, then the pop for that node did not finish yet and we can return
      // WasFalse. If we see that m_tail points to m_stub then the call to pop
      // for the previous node might have only JUST finished. However, in that
      // case we can return True (resulting in no call to wait) because a
      // subsequent signal will simply be ignored [or cause this task to continue
      // running if somehow it tried to get the lock again (later) and fail to
      // get it (causing a call to wait then); however that is not possible: we
      // ARE at the front of the queue and that can not change no matter what
      // other threads do].
      //
      bool is_front = prev == &m_stub && m_tail.load(std::memory_order_acquire) == &m_stub;

      // Here m_head points to the new node, which either points to null
      // or already points to the NEXT node that was pushed AND completed, etc.
      // Now fix the next pointer of the node that m_head was pointing at.
      prev->m_next.store(node, std::memory_order_release);

      return is_front ? fuzzy::True : fuzzy::WasFalse;
    }

    // This function may only be called by the consumer!
    //
    // Returns the node that will be returned by the next call to pop.
    //
    // If nullptr is returned then the push, of the next non-null node that will be returned by
    // pop, didn't complete yet (or wasn't even called yet).
    //
    // If &m_stub is returned then the push, of the next non-null node that will be returned by
    // pop wasn't called yet and the list is empty.
    MpscNode const* peek() const
    {
      // If m_tail is not pointing to m_stub then it points to the node that will be
      // returned by a call to pop.
      //
      // If m_head is not pointing to m_stub (and m_tail is) then m_stub->m_next is the
      // next node that will be returned by a call to pop. If this m_next value is still
      // nullptr then an on going push (that changed the value of m_head away from
      // m_stub) didn't complete yet. In that case we wait until this m_next pointer is
      // updated.
      //
      // If m_head points to m_stub (and m_tail too) then the queue is empty;
      // m_stub.m_next is guaranteed to be null too in that case. We just return
      // nullptr then.
      MpscNode const* tail = m_tail.load(std::memory_order_relaxed);
      if (tail != &m_stub)
        return tail;
      MpscNode const* head = m_head.load(std::memory_order_relaxed);
      if (head == &m_stub)
          return nullptr;
      // Wait for the current push to complete.
      MpscNode const* next;
      while (!(next = m_stub.m_next.load(std::memory_order_acquire)))
        cpu_relax();
      return next;
    }

    // This function is only called by the task that owns node; in other words,
    // node is the queue and will not be popped for the duration of this call.
    utils::FuzzyBool is_front(MpscNode const* node) const
    {
      MpscNode const* tail = m_tail.load(std::memory_order_relaxed);
      return (tail == node || tail == &m_stub && m_stub.m_next.load(std::memory_order_acquire) == node) ? fuzzy::True : fuzzy::WasFalse;
    }
  };

 public:
  /// Returns the size of the nodes that will be allocated from s_node_memory_resource.
  static constexpr size_t node_size() { return sizeof(Node); }
  /// This must be called once before using a AIStatefulTaskMutex.
  static void init(utils::MemoryPagePool* mpp_ptr) { s_node_memory_resource.init(mpp_ptr, node_size()); }
  static utils::NodeMemoryResource s_node_memory_resource;      ///< Memory resource to allocate Node's from.

 private:
  Queue m_queue;

 public:
  /// Construct an unlocked AIStatefulTaskMutex.
  AIStatefulTaskMutex() { }
  // Immediately after construction, nobody owns the lock:
  //
  // m_head --> m_stub

  /// Try to obtain ownership for owner (recursive locking allowed).
  ///
  /// @returns A handle pointer upon success and nullptr upon failure to obtain ownership.
  ///
  /// The returned handle must be passed to is_self_locked.
  inline Node const* lock(AIStatefulTask* task, condition_type condition);

  /// Undo one (succcessful) call to lock.
  void unlock();

#if CW_DEBUG
  // This is obviously a racy condition, unless called by the only consumer thread;
  // which would be the thread running the task that currently has the lock, so if
  // we know that that is the case then we don't need to call this function :p.
  //
  // Most notably, theoretically a task could be returned that is deleted by the time
  // you use it. So, don't use the result. It is intended only for debug output
  // (printing the returned pointer).
  AIStatefulTask* debug_get_owner() const
  {
    AIStatefulTaskMutexNode const* next = static_cast<AIStatefulTaskMutexNode const*>(m_queue.peek());
    // next might get deallocated right here, but even if that is the case then this still
    // isn't UB since it is allocated from a utils::SimpleSegregatedStorage which never
    // *actually* frees memory. At most next->m_task is a non-sensical value, although the
    // chance for that is extremely small.
    return next ? next->m_task : nullptr;
  }
#endif

 private:
  friend class AIStatefulTask;
  // Is this object currently owned (locked) by us?
  // May only be called from some multiplex_impl passing its own AIStatefulTask pointer,
  // and therefore only by AIStatefulTask::is_self_locked(AIStatefulTaskMutex&).
  //
  //   case MyTask_lock:
  //     set_state(MyTask_locked);
  //     if (!(m_handle = the_mutex.lock(this, MyTask_locked)))
  //     {
  //       wait(1);
  //       break;
  //     }
  //     [[fallthrough]];
  //   case MyTask_locked:
  //   {
  //     statefultask::Lock lock(the_mutex);
  //     ...
  //     if (is_self_locked(the_mutex, m_handle))
  //      ...
  utils::FuzzyBool is_self_locked(Node const* handle) const
  {
    return m_queue.is_front(handle);
  }
};

namespace statefultask {

// Convenience class to automatically unlock the mutex upon leaving the current scope.
class AdoptLock
{
 private:
  AIStatefulTaskMutex* m_mutex;

 public:
  AdoptLock(AIStatefulTaskMutex& mutex) : m_mutex(&mutex) { }
  ~AdoptLock() { unlock(); }

  void unlock()
  {
    if (m_mutex)
      m_mutex->unlock();
    m_mutex = nullptr;
  }

  void skip_unlock()
  {
    m_mutex = nullptr;
  }
};

} // namespace statefultask

#include "AIStatefulTask.h"
#endif // AISTATEFULTASKMUTEX_H

#ifndef AISTATEFULTASKMUTEX_H_definitions
#define AISTATEFULTASKMUTEX_H_definitions

AIStatefulTaskMutex::Node const* AIStatefulTaskMutex::lock(AIStatefulTask* task, condition_type condition)
{
  DoutEntering(dc::notice, "AIStatefulTaskMutex::lock(" << task << ", " << task->print_conditions(condition) << ") [mutex:" << this << "]");

  Node* new_node = new (s_node_memory_resource.allocate(sizeof(Node))) Node(task, condition);
  Dout(dc::notice, "Create new node at " << new_node << " [" << task << "]");

  utils::FuzzyBool have_lock = m_queue.push(new_node);

  // If the next return from pop() will return new_node, then have_lock MUST be True.
  // If the next return from pop() will return another node then have_lock MUST be WasFalse.
  if (have_lock.is_true())
  {
    Dout(dc::notice, "Mutex acquired [" << task << "]");
    return new_node;
  }
  Dout(dc::notice, "Mutex already locked [" << task << "]");

  // Obtaining the lock failed. Halt the task
  return nullptr;     // The caller must call task->wait(condition).
}

#endif // AISTATEFULTASKMUTEX_H_definitions
