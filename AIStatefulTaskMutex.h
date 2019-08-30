/**
 * @file
 * @brief Mutex for stateful tasks. Declaration of class AIStatefulTaskMutex.
 *
 * @Copyright (C) 2016, 2017  Carlo Wood.
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
 *   12/12/2016
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#pragma once

#include "threadsafe/aithreadsafe.h"
#include "debug.h"

class AIStatefulTask;

//
// A task mutex.
//
// Prevent different tasks from concurrently entering the same critical area.
//
// Consider an object that is shared between tasks, but may
// not be simultaneously accessed by two different threads.
//
// For example,
//
//   struct Shared
//   {
//     AIStatefulTaskMutex m_task_mutex;
//     void do_work();    // Only one thread at a time may call this.
//   };
//
//   Shared shared;
//
// Then multiple running tasks could use this to prevent concurrent access:
//
// ...
//   case MyTask_stateX:
//     if (!shared.m_task_mutex.try_lock(this))
//     {
//       yield();
//       break;
//     }
//     shared.do_work();
//     shared.m_task_mutex.unlock();
//
class AIStatefulTaskMutex
{
 protected:
  using lock_count_ts = aithreadsafe::Wrapper<int, aithreadsafe::policy::Primitive<std::mutex>>;

  lock_count_ts m_lock_count;                   // Number of times unlock must be callled before unlocked.
  AIStatefulTask const* m_owner;                // Owner of the lock. Only valid when m_lock_count > 0; protected by the mutex of m_lock_count.

 public:
  // Construct an unlocked AIStatefulTaskMutex.
  AIStatefulTaskMutex() : m_lock_count(0), m_owner(nullptr) { }

  // Try to obtain ownership for owner (recursive locking allowed).
  // Returns true upon success and false upon failure to obtain ownership.
  bool try_lock(AIStatefulTask const* owner)
  {
    lock_count_ts::wat lock_count_w(m_lock_count);
    if (*lock_count_w > 0 && m_owner != owner)
      return false;
    m_owner = owner;
    ++*lock_count_w;
    return true;
  }

  // Undo one (succcessful) call to try_lock.
  // The AIStatefulTask that is being passed must own the lock.
  void unlock(AIStatefulTask const* owner)
  {
    lock_count_ts::wat lock_count_w(m_lock_count);
    ASSERT(*lock_count_w > 0 && m_owner == owner);
    --*lock_count_w;
  }

 private:
  friend class AIStatefulTask;
  // Is this object currently owned (locked) by us?
  // May only be called from some multiplex_impl passing its own AIStatefulTask pointer,
  // and therefore only by AIStatefulTask::is_self_locked(AIStatefulTaskMutex&).
  //
  //   case SomeState:
  //     ...
  //     if (is_self_locked(the_mutex))
  //      ...
  bool is_self_locked(AIStatefulTask const* caller) const
  {
    lock_count_ts::crat lock_count_w(m_lock_count);
    return *lock_count_w > 0 && m_owner == caller;
  }
};
