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
