/**
 * @file
 * @brief Implementation of AIBackgroundJob.
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

#include "sys.h"
#include "debug.h"
#include "AIBackgroundJob.h"

AIBackgroundJob::~AIBackgroundJob()
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
void AIBackgroundJob::run()
{
  Debug(NAMESPACE_DEBUG::init_thread());
  m_job();
  m_phase = finished;
  m_parent_task->signal(m_condition);
}

// Called by parent task to dispatch the job to its own thread.
// After finishing the job, the parent will continue in new_state.
void AIBackgroundJob::execute_and_continue_at(int new_state)
{
  m_phase = executing;
  m_thread = std::thread(&AIBackgroundJob::run, this);
  wait_until([this](){ return m_phase == finished; }, m_condition, new_state);
}
