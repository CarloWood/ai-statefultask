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

class AIBackgroundJob : AIFriendOfStatefulTask {
  private:
    using FunctionType = std::function<void()>;

    std::thread m_thread;                               // Associated with the thread running m_job if m_phase == executing.
    enum { standby, executing, finished } m_phase;      // Keeps track of whether the job is already executing or even finished.
    FunctionType m_job;
    AIStatefulTask::condition_type m_condition;

  public:
    AIBackgroundJob(AIStatefulTask* parent_task, AIStatefulTask::condition_type condition, FunctionType const& function) :
        AIFriendOfStatefulTask(parent_task), m_phase(standby), m_job(function), m_condition(condition) { }
    ~AIBackgroundJob();
    void run();
    void execute_and_continue_at(int new_state);
};
