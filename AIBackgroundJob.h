/**
 * @file
 * @brief Run code in a thread. Declaration of template class AIBackgroundJob.
 *
 * Copyright (C) 2017  Carlo Wood.
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

#ifdef EXAMPLE_CODE     // undefined

class Task : public AIStatefulTask {
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
    static state_type const max_state = Task_done + 1;  // One beyond the largest state.
    Task() : AIStatefulTask(DEBUG_ONLY(true)),
        m_long_job(this, 1, blocking_function) { }      // Prepare to run `blocking_function' in its own thread.

  private:
    AIBackgroundJob m_long_job;
};

void Task::multiplex_impl(state_type run_state)
{
  switch(run_state)
  {
    case Task_start:
    {
      m_long_job.execute_and_continue_at(Task_done);	// Execute the function `blocking_function' in its own thread
							// and continue running this task at state Task_done once
							// `blocking_function' has finished executing.
      break;
    }
    case Task_done:
      finish();
      break;
  }
}
#endif // EXAMPLE_CODE

template<typename ...Args>
class AIBackgroundJob : AIFriendOfStatefulTask {
  private:
    std::thread m_thread;                               // Associated with the thread running m_job if m_phase == executing.
    enum { standby, executing, finished } m_phase;      // Keeps track of whether the job is already executing or even finished.
    AIStatefulTask::condition_type m_condition;
    AIDelayedFunction<Args...> m_delayed_function;

  public:
    AIBackgroundJob(AIStatefulTask* parent_task, AIStatefulTask::condition_type condition, void (*fp)(Args...)) :
        AIFriendOfStatefulTask(parent_task), m_phase(standby), m_condition(condition), m_delayed_function(fp) { }

    template<class C>
    AIBackgroundJob(AIStatefulTask* parent_task, AIStatefulTask::condition_type condition, C* object, void (C::*memfn)(Args...)) :
        AIFriendOfStatefulTask(parent_task), m_phase(standby), m_condition(condition), m_delayed_function(object, memfn) { }

    ~AIBackgroundJob();
    void run();
    void operator()(Args... args);
};

template<typename ...Args>
AIBackgroundJob<Args...>::~AIBackgroundJob()
{
  // It should be impossible to destruct an AIBackgroundJob while it is still
  // executing when it is a member of parent_task; and that is the only way
  // that this class should be used. The reason that is impossible is because the
  // parent_task should be in a waiting state until we call m_parent_task->signal(m_condition)
  // in run() below, which we only do after m_phase is set to finished. Hence,
  // the parent_task will not be destructed and therefore we won't be destructed either.
  ASSERT(m_phase != executing);
  // Wait until the thread actually returned from run().
  m_thread.join();
}

// This is executed in the new thread.
template<typename ...Args>
void AIBackgroundJob<Args...>::run()
{
  Debug(NAMESPACE_DEBUG::init_thread());
  m_delayed_function.invoke();
  m_phase = finished;
  m_parent_task->signal(m_condition);
}

// Called by parent task to dispatch the job to its own thread.
// After finishing the job, the parent will be signalled with
// m_condition set during construction.
template<typename ...Args>
void AIBackgroundJob<Args...>::operator()(Args... args)
{
  // Store arguments.
  m_delayed_function(args...);
  // Pass job to a thread.
  m_phase = executing;
  m_thread = std::thread(&AIBackgroundJob::run, this);
  // Halt task until job finished.
  wait_until([this](){ return m_phase == finished; }, m_condition);
}
