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
 */

#pragma once

#include "threadsafe/aithreadsafe.h"
#include "threadsafe/AIRecursiveMutex.h"
#include <deque>
#include <boost/intrusive_ptr.hpp>

class AIStatefulTask;

// class AICondition
//
// Call AIStatefulTask::wait(AICondition&) in the multiplex_impl of a task to
// make the task go idle until some thread calls AICondition::signal().
//
// If the task is no longer running or wasn't waiting anymore because
// something else woke it up, then AICondition::signal() will wake up another
// task (if any).
//
// Usage:
//
// struct Foo { bool met(); };  // Returns true when the condition is met.
// typedef AICondition<Foo> FooCondition;
//
// // Some thread-safe condition variable.
// FooCondition condition;
//
// // Inside the task:
// {
//   ...
//   state WAIT_FOR_CONDITION:
//   {
//     // Lock condition and check it. Wait if condition is not met yet.
//     {
//       FooCondition::wat condition_w(condition);
//       if (!condition_w->met())
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

class AIConditionBase
{
  public:
    AIConditionBase();
    virtual ~AIConditionBase();

    void signal(int n = 1);                                             // Call this when the condition was met to release n tasks.
    void broadcast() { signal(mWaitingStatefulTasks.size()); }          // Release all blocked tasks.

  private:
    // These functions are called by AIStatefulTask.
    friend class AIStatefulTask;
    void wait(AIStatefulTask* stateful_task);
    void remove(AIStatefulTask* stateful_task);

  protected:
    virtual AIRecursiveMutex& mutex() = 0;

  protected:
    typedef std::deque<boost::intrusive_ptr<AIStatefulTask> > queue_type;
    queue_type mWaitingStatefulTasks;
};

template<typename T>
class AICondition : public aithreadsafe::Wrapper<T, aithreadsafe::policy::Primitive<AIRecursiveMutex>>, public AIConditionBase
{
  protected:
    /*virtual*/ AIRecursiveMutex& mutex() { return this->mMutex; }
};
