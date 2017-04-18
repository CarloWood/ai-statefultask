/**
 * @file
 * @brief Base class of classes that extend AIStatefulTask derived tasks as member function.
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
 *   18/04/2017
 *   - Initial version, written by Carlo Wood.
 */

#pragma once

#include "AIStatefulTask.h"

#ifdef EXAMPLE_CODE     // undefined

class MyTool : public AIFriendOfStatefulTask {
  public:
    // Parent is the (base class of the) class containing this MyTool as member object.
    MyTool(AIStatefulTask* parent) : AIFriendOfStatefulTask(parent) { }

    void f()
    {
      // Now we can call the protected control functions set_state, wait, wait_until,
      // finish, yield, target, yield_frame, yield_ms and yield_if_not.
      yield();
    }
};

#endif  // EXAMPLE CODE

class AIFriendOfStatefulTask {
  public:
    using state_type = AIStatefulTask::state_type;
    using condition_type = AIStatefulTask::condition_type;

  protected:
    AIStatefulTask* m_parent_task;

    AIFriendOfStatefulTask(AIStatefulTask* parent_task) : m_parent_task(parent_task) { }

    void set_state(state_type new_state) { m_parent_task->set_state(new_state); }
    void wait(condition_type conditions) { m_parent_task->wait(conditions); }
    void wait_until(AIWaitConditionFunc const& wait_condition, condition_type conditions) { m_parent_task->wait_until(wait_condition, conditions); }
    void wait_until(AIWaitConditionFunc const& wait_condition, condition_type conditions, state_type new_state) { m_parent_task->set_state(new_state); m_parent_task->wait_until(wait_condition, conditions); }
    void finish() { m_parent_task->finish(); }
    void yield() { m_parent_task->yield(); }
    void target(AIEngine* engine) { m_parent_task->target(engine); }
    void yield(AIEngine* engine) { m_parent_task->yield(engine); }
    void yield_frame(unsigned int frames) { m_parent_task->yield_frame(frames); }
    void yield_ms(unsigned int ms) { m_parent_task->yield_ms(ms); }
    bool yield_if_not(AIEngine* engine) { return m_parent_task->yield_if_not(engine); }
};
