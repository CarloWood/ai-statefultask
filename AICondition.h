/**
 * @file
 * @brief Condition variable for stateful tasks. Declaration of class AICondition.
 *
 * Copyright (C) 2013, 2017  Carlo Wood.
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
 *   14/10/2013
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 *
 *   2017/03/17
 *   - Rewrite.
 */

#pragma once

#include <mutex>
#include "debug.h"
#include <boost/intrusive_ptr.hpp>

class AIStatefulTask;

// class AICondition
//
// Call AIStatefulTask::wait(AICondition&) in the multiplex_impl of a task to
// make the task go idle until some thread calls AICondition::signal().
//
// Usage:
//
// Declaration in a task:
//
// class MyTask : public AIStatefulTask {
// ...
//   AICondition m_condition1;
// ...
//   MyTask() : m_condition1(*this) { }
//
// // Inside the task:
// {
//   ...
//   state WAIT_FOR_CONDITION1:
//   {
//     // Lock condition and check it. Wait if condition is not met yet.
//     {
//       if (!Foo::rat(foo)->met())
//       {
//         wait(condition);
//         break;
//       }
//     }
//     set_state(CONDITION_MET);
//     break;
//   }
//   CONDITION_MET:
//   {
//

class AICondition
{
  private:
    boost::intrusive_ptr<AIStatefulTask> m_task;        // Pointer to the owning task.
    std::mutex m_mutex;                                 // Mutex protecting m_not_idle and m_skip_idle.
    bool m_not_idle;
    bool m_skip_idle;
    bool m_idle;                                        // A copy of !m_not_idle made when wait() was last called.

  public:
    // Call this to wake up the task iff it is (still) waiting on this condition.
    void signal();

  public:
    AICondition(AIStatefulTask* owner) : m_task(owner), m_not_idle(true), m_skip_idle(false), m_idle(false) { }

  private:
    friend class AIStatefulTask;
    void wait() { std::lock_guard<std::mutex> lock(m_mutex); ASSERT(m_not_idle); m_not_idle = m_skip_idle; m_skip_idle = false; m_idle = !m_not_idle; }
    bool idle() { return m_idle; }
};
