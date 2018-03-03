/**
 * @file
 * @brief Run code in a thread. Declaration of template class AIPackagedTask.
 *
 * @Copyright (C) 2017  Carlo Wood.
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
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   19/04/2017
 *   - Initial version, written by Carlo Wood.
 */

#pragma once

#include "AIFriendOfStatefulTask.h"
#include "AIDelayedFunction.h"
#include "AIObjectQueue.h"
#include "AIThreadPool.h"

#ifdef EXAMPLE_CODE     // undefined

int factorial(int n)
{
  int r = 1;
  while(n > 1) r *= n--;
  return r;
}

class Task : public AIStatefulTask
{
 protected:
  using direct_base_type = AIStatefulTask;            // The base class of this task.
  ~Task() override { }                                // The destructor must be protected.

  // The different states of the task.
  enum task_state_type {
    Task_start = direct_base_type::max_state,
    Task_done,
  };

  // Override virtual functions.
  char const* state_str_impl(state_type run_state) const override;
  void multiplex_impl(state_type run_state) override;

 public:
  static state_type constexpr max_state = Task_done + 1;      // One beyond the largest state.
  Task() : AIStatefulTask(DEBUG_ONLY(true)),
      m_calculate_factorial(this, 1, &factorial) { }          // Prepare to run `factorial' in its own thread.

 private:
  AIPackagedTask<int(int)> m_calculate_factorial;
};

void Task::multiplex_impl(state_type run_state)
{
  switch(run_state)
  {
    case Task_start:
    {
      m_calculate_factorial(5);                         // "Call the function" -- this just copies the argument(s) to be passed to the executing thread.
      set_state(Task_dispatch);
    }
    case Task_dispatch:
      if (!m_calculate_factorial.dispatch())            // Execute the function `factorial' in its own thread.
      {
        yield_frames(1);
        break;
      }
      set_state(Task_done);                             // and continue running this task at state Task_done once
      break;                                            // `factorial' has finished executing.
    }
    case Task_done:
      std::cout << "The factorial of 5 = " << m_calculate_factorial.get() << std::endl;
      finish();
      break;
  }
}
#endif // EXAMPLE_CODE

#ifndef DOXYGEN
template<typename F>
class AIPackagedTask;   // not defined.
#endif

/*!
 * @brief A wrapper for a <em>(member) function</em> to be executed by the AIThreadPool.
 *
 * An AIPackagedTask is supposed to be a member of a class (e.g. <code>MyTask</code>) derived from AIStatefulTask.
 * The <code>this</code> pointer of that task should be passed to the constructor, along with a function to
 * be called and a handle of the queue to add it to (as obtained by a call to <code>AIThreadPool::new_queue()</code>).
 *
 * It is possible to use a pointer to a free function or to the member function of a given object.
 * In both cases with arbitrary signature.
 *
 * For example, suppose you have some object \c foo of type \c Foo with a member function
 * called <code>retrieve</code> that has the signature <code>bool Foo::retrieve(int, double)</code>,
 * whatever those parameters may mean. Then you could do:
 *
 * @code
 * class MyTask : public AIStatefulTask
 * {
 *   AIPackagedTask<bool(int, double)> m_retrieve;              // Space holder for all variables involved.
 *   static condition_type constexpr retrieve_condition = 1;    // The condition bit to be used.
 *  public:
 *   MyTask(AIQueueHandle queue_handle) : m_retrieve(this, retrieve_condition, &foo, &Foo::retrieve, queue_handle) { }
 * ...
 * };
 * @endcode
 *
 * where as @link waiting usual@endlink \c retrieve_condition should be different from any other condition
 * bits that this task is using. And where \c queue_handle is a handle
 * that was previously returned by AIThreadPool::new_queue().
 *
 * Then this function can be called (repeatedly, with different parameters
 * if need be) from \c multiplex_impl as follows:
 *
 * @code
 * ...
 *   case MyTask_state20:
 *     m_retrieve(n, y);                   // Copy parameters to m_retrieve.
 *     set_state(MyTask_dispatch);
 *   case MyTask_dispatch:
 *     if (!m_retrieve.dispatch())         // Put m_retrieve in the queue.
 *     {
 *       yield_frames(1);                  // Yield because the queue was full.
 *       break;
 *     }
 *     set_state(MyTask_state21);          // Continue with state21 once the
 *     break;                              //   function finished executing.
 *   case MyTask_state21:
 *   {
 *     bool result = m_retrieve.get();     // Get the result.
 *
 * @endcode
 */
template<typename R, typename ...Args>
class AIPackagedTask<R(Args...)> : AIFriendOfStatefulTask
{
 private:
  enum { standby, deferred, executing, finished } m_phase;  // Keeps track of whether the job is already executing or even finished.
  AIStatefulTask::condition_type m_condition;
  AIDelayedFunction<R(Args...)> m_delayed_function;
  AIQueueHandle m_queue_handle;

 public:
  /*!
   * @brief Construct a packaged task for a free function.
   *
   * @param parent_task The task that \ref dispatch will call \ref wait_until and @link AIStatefulTask::signal signal@endlink on.
   * @param condition The condition to use for \ref wait_until and @link AIStatefulTask::signal signal@endlink.
   * @param fp A pointer to the function that needs to be called.
   * @param object_queue_handle A handle to the AIObjectQueue that the delayed function should be placed in.
   */
  AIPackagedTask(AIStatefulTask* parent_task, AIStatefulTask::condition_type condition, R (*fp)(Args...), AIQueueHandle object_queue_handle) :
      AIFriendOfStatefulTask(parent_task), m_phase(standby), m_condition(condition), m_delayed_function(fp), m_queue_handle(object_queue_handle) { }

  /*!
   * @brief Construct a packaged task for a member function.
   *
   * @param parent_task The task that \ref dispatch will call \ref wait_until and @link AIStatefulTask::signal signal@endlink on.
   * @param condition The condition to use for \ref wait_until and @link AIStatefulTask::signal signal@endlink.
   * @param object Pointer to the object of which the member function must be called.
   * @param memfp A pointer to the member function that needs to be called.
   * @param object_queue_handle A handle to the AIObjectQueue that the delayed function should be placed in.
   */
  template<class C>
  AIPackagedTask(AIStatefulTask* parent_task, AIStatefulTask::condition_type condition, C* object, R (C::*memfp)(Args...), AIQueueHandle object_queue_handle) :
      AIFriendOfStatefulTask(parent_task), m_phase(standby), m_condition(condition), m_delayed_function(object, memfp), m_queue_handle(object_queue_handle) { }

  //! Destructor.
  ~AIPackagedTask();

  //! Exchange the state with that of \a other.
  void swap(AIPackagedTask& other) noexcept
  {
    std::swap(m_condition, other.m_condition);
    m_delayed_function.swap(other.m_delayed_function);
    std::swap(m_queue_handle, other.m_queue_handle);
  }

  //! Copy the arguments.
  void operator()(Args... args);

  /*!
   * @brief Put the task in a queue for execution in a different thread.
   *
   * Actually queue the task in the AIObjectQueue whose handle was passed to the constructor
   * and halt the \c parent_task as passed to the constructor until this task is finished.
   *
   * @returns True if the task was successfully queued; false if the queue was full.
   */
  bool dispatch();

#ifndef DOXYGEN
  // If Args isn't empty (has_args) then we need this signature in order to be Callable.
  template<bool has_args = sizeof... (Args) != 0, typename std::enable_if<has_args, int>::type = 0>
  void operator()();                                  // Invoke the function.
#endif

  /*!
   * @brief Read out the result of the function.
   *
   * May only be called after <code>parent_task->signal(condition)</code> was called.
   */
  R get() const
  {
    ASSERT(m_phase == finished);                      // Call dispatch() until it returns true, before calling get().
    return m_delayed_function.get();                  // Get the result.
  }

 private:
  void invoke();
};

template<typename R, typename ...Args>
AIPackagedTask<R(Args...)>::~AIPackagedTask()
{
  // It should be impossible to destruct an AIPackagedTask while it is still
  // executing when it is a member of parent_task; and that is the only way
  // that this class should be used. The reason that is impossible is because the
  // parent_task should be in a waiting state until we call m_task->signal(m_condition)
  // in invoke() below, which we only do after m_phase is set to finished. Hence,
  // the parent_task will not be destructed and therefore we won't be destructed either.
  ASSERT(m_phase != executing);
}

// Invoke the function (inlined because it's used in two places below).
template<typename R, typename ...Args>
inline void AIPackagedTask<R(Args...)>::invoke()
{
  m_delayed_function.invoke();
  m_phase = finished;
  m_task->signal(m_condition);
}

// This is executed by a different thread.
template<typename R, typename ...Args>
template<bool has_args, typename std::enable_if<has_args, int>::type>
void AIPackagedTask<R(Args...)>::operator()()
{
  invoke();
}

// Store the function arguments (or invoke task from executing thread).
template<typename R, typename ...Args>
void AIPackagedTask<R(Args...)>::operator()(Args... args)
{
  // When sizeof...(Args) == 0 then the second time this function is called is by executing thread.
  // The first call has to be fast, so assume it's unlikely.
  if (sizeof...(Args) == 0 &&
      AI_UNLIKELY(m_phase == executing))
  {
    invoke();
    return;
  }

  // If m_phase == deferred then you should have called retry().
  ASSERT(m_phase == standby);

  // Store arguments.
  m_delayed_function(args...);
}

// Called by parent task to dispatch the job to its own thread.
// After finishing the job, the parent will be signalled with
// m_condition set during construction.
//
// Returns true upon a successful queue; false when the queue is full
// in which case dispatch() can be tried again (a bit later).
template<typename R, typename ...Args>
bool AIPackagedTask<R(Args...)>::dispatch()
{
  {
    // Stop a new queue from being created while we're working with a queue, because that could move the queue.
    AIThreadPool& thread_pool = AIThreadPool::instance();
    auto queues_r = thread_pool.queues_read_access();
    // Lock the queue.
    auto& queue_ref = thread_pool.get_queue(queues_r, m_queue_handle);
    {
      auto queue = queue_ref.producer_access();
      if (queue.length() == queue_ref.capacity())
      {
        m_phase = deferred;
        return false;
      }
      // Pass job to thread pool.
      m_phase = executing;
      queue.move_in(std::function<bool()>([this](){ this->invoke(); return false; }));
    } // Unlock queue.
    // Now that we added something to queue, wake up one thread if needed.
    queue_ref.notify_one();
  } // And we're done with the queue, so also unlock AIThreadPool::m_queues.

  // Halt task until job finished.
  wait_until([this](){ return m_phase == finished; }, m_condition);
  return true;
}
