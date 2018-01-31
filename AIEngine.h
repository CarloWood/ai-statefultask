/**
 * @file
 * @brief Declaration of class AIEngine.
 *
 * Copyright (C) 2010 - 2013, 2017  Carlo Wood.
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
 *   01/03/2010
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   28/02/2013
 *   - Rewritten from scratch to fully support threading.
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#pragma once

#include "threadsafe/aithreadsafe.h"
#include "threadsafe/Condition.h"
#include "AIStatefulTask.h"
#include "debug.h"
#include <list>
#include <chrono>
#include <boost/intrusive_ptr.hpp>

/*!
 * @brief AIStatefulTask multiplexer.
 *
 * This object dispatches tasks from its main loop.
 */
class AIEngine
{
 private:
  struct QueueElementComp;
  class QueueElement {
    private:
      boost::intrusive_ptr<AIStatefulTask> mStatefulTask;

    public:
      QueueElement(AIStatefulTask* stateful_task) : mStatefulTask(stateful_task) { }
      friend bool operator==(QueueElement const& e1, QueueElement const& e2) { return e1.mStatefulTask == e2.mStatefulTask; }
      friend bool operator!=(QueueElement const& e1, QueueElement const& e2) { return e1.mStatefulTask != e2.mStatefulTask; }
      friend struct QueueElementComp;

      AIStatefulTask const& stateful_task() const { return *mStatefulTask; }
      AIStatefulTask& stateful_task() { return *mStatefulTask; }
  };
  struct QueueElementComp {
    inline bool operator()(QueueElement const& e1, QueueElement const& e2) const;
  };

  using queued_type = std::list<QueueElement>;

  struct engine_state_st
  {
    queued_type list;
    bool waiting;
    engine_state_st() : waiting(false) { }
  };

  using engine_state_type = aithreadsafe::Wrapper<engine_state_st, aithreadsafe::policy::Primitive<aithreadsafe::Condition>>;

#ifndef DOXYGEN
 public:       // Used by AIStatefulTask.
  using clock_type = AIStatefulTask::clock_type;
  using duration_type = AIStatefulTask::duration_type;
#endif

 private:
  engine_state_type mEngineState;
  char const* mName;
  duration_type mMaxDuration;
  bool mHasMaxDuration;

 public:
  /*!
   * @brief Construct an AIEngine.
   *
   * The argument \a name must be a string-literal (only the pointer to it is stored).
   * If \a max_duration is less than or equal zero (the default) then no duration is set
   * and the engine won't return from \ref mainloop until all tasks in its queue either
   * finished, are waiting (idle) or did yield to a different engine.
   *
   * @param name A human readable name for this engine. Mainly used for debug output.
   * @param max_duration The maximum duration for which new tasks are run per loop. See SetMaxDuration.
   */
  AIEngine(char const* name, float max_duration = 0.0f) : mName(name) { setMaxDuration(max_duration); }

  /*!
   * @brief Add \a stateful_task to this engine.
   *
   * The task will remain assigned to the engine until it no longer @link AIStatefulTask::active active@endlink
   * (tested after returning from @link Example::multiplex_impl multiplex_impl@endlink).
   *
   * Normally you should not call this function directly. Instead, use @link group_run AIStatefulTask::run@endlink.
   *
   * @param stateful_task The task to add.
   */
  void add(AIStatefulTask* stateful_task);

  /*!
   * @brief The main loop of the engine.
   *
   * Run all tasks that were @link add added@endlink to the engine until
   * they are all finished and/or idle, or until mMaxDuration milliseconds
   * have passed if a maximum duration was set.
   */
  void mainloop();

  /*!
   * @brief Wake up a sleeping engine.
   */
  void wake_up();

  /*!
   * @brief Flush all tasks from this engine.
   *
   * All queued tasks are removed from the engine and marked as killed.
   * This can be used when terminating a program, just prior to destructing
   * all remaining objects, to avoid that tasks do call backs and use objects
   * that are being destructed.
   */
  void flush();

  /*!
   * @brief Return a human readable name of this engine.
   *
   * This is simply the string that was passed upon construction.
   */
  char const* name() const { return mName; }

  /*!
   * @brief Set mMaxDuration in milliseconds.
   *
   * The maximum time the engine will spend in \ref mainloop calling \c multiplex on unfinished and non-idle tasks.
   * Note that if the last call to \c multiplex takes considerable time then it is possible that the time spend
   * in \c mainloop will go arbitrarily far beyond \c mMaxDuration. It is the responsibility of the user to not
   * run states (of task) that can take too long in engines that have an \c mMaxDuration set.
   */
  void setMaxDuration(float max_duration);

  /*!
   * @brief Return true if a maximum duration was set.
   *
   * Note, only engines with a set maximum duration can be used to sleep
   * on by using AIStatefulTask::yield_frame or AIStatefulTask::yield_ms.
   */
  bool hasMaxDuration() const { return mHasMaxDuration; }
};
