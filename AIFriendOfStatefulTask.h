/**
 * @file
 * @brief Base class of classes that extend AIStatefulTask derived tasks as member function.
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
 *   18/04/2017
 *   - Initial version, written by Carlo Wood.
 */

#pragma once

#include "AIStatefulTask.h"

/*!
 * A hook to the protected control functions of AIStatefulTask.
 *
 * Allows to call the @link group_protected protected control functions@endlink of a task.
 *
 * Usage:
 *
 * @code
 * class MyTool : public AIFriendOfStatefulTask
 * {
 *  public:
 *   // Task is the (base class of the) class containing this MyTool as member object.
 *   MyTool(AIStatefulTask* task) : AIFriendOfStatefulTask(task) { }
 *
 *   void f()
 *   {
 *     // Now we can call the protected control functions of `task`.
 *     yield();
 *   }
 * };
 * @endcode
 */
class AIFriendOfStatefulTask
{
 public:
  using state_type = AIStatefulTask::state_type;              //!< Proxy for AIStatefulTask::state_type.
  using condition_type = AIStatefulTask::condition_type;      //!< Proxy for AIStatefulTask::condition_type.

 protected:
  AIStatefulTask* m_task;     //!< The base class of the object that this object is a member of.

  //! Construct a friend of @a task.
  AIFriendOfStatefulTask(AIStatefulTask* task) : m_task(task) { }

  //! Proxy for AIStatefulTask::set_state.
  void set_state(state_type new_state) { m_task->set_state(new_state); }
  //! Proxy for AIStatefulTask::wait.
  void wait(condition_type conditions) { m_task->wait(conditions); }
  //! Proxy for AIStatefulTask::wait_until.
  void wait_until(AIWaitConditionFunc const& wait_condition, condition_type conditions) { m_task->wait_until(wait_condition, conditions); }
  //! Proxy for AIStatefulTask::wait_until.
  void wait_until(AIWaitConditionFunc const& wait_condition, condition_type conditions, state_type new_state) { m_task->set_state(new_state); m_task->wait_until(wait_condition, conditions); }
  //! Proxy for AIStatefulTask::finish.
  void finish() { m_task->finish(); }
  //! Proxy for AIStatefulTask::yield.
  void yield() { m_task->yield(); }
  //! Proxy for AIStatefulTask::target.
  void target(AIEngine* engine) { m_task->target(engine); }
  //! Proxy for AIStatefulTask::yield.
  void yield(AIEngine* engine) { m_task->yield(engine); }
  //! Proxy for AIStatefulTask::yield_frame.
  void yield_frame(AIEngine* engine, unsigned int frames) { m_task->yield_frame(engine, frames); }
  //! Proxy for AIStatefulTask::yield_ms.
  void yield_ms(AIEngine* engine, unsigned int ms) { m_task->yield_ms(engine, ms); }
  //! Proxy for AIStatefulTask::yield_if_not.
  bool yield_if_not(AIEngine* engine) { return m_task->yield_if_not(engine); }
};
