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
  static duration_type sMaxDuration;

 public:
  /*!
   * @brief Construct an AIEngine.
   *
   * The argument \a name must be a string-literal (only the pointer to it is stored).
   *
   * @param name A human readable name for this engine. Mainly used for debug output.
   */
  AIEngine(char const* name) : mName(name) { }

  /*!
   * @brief Add \a stateful_task to this engine.
   *
   * The task will remain assigned to the engine until it no longer @link AIStatefulTask::active active@endlink
   * (tested after returning from @link Example::multiplex_impl multiplex_impl@endlink).
   *
   * @param stateful_task The task to add.
   */
  void add(AIStatefulTask* stateful_task);

  /*!
   * @brief The main loop of the engine.
   *
   * Run all tasks that were @link add added@endlink to the engine until
   * they are all finished and/or idle.
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
   * @brief Set sMaxDuration in milliseconds.
   *
   * The maximum time the engine will spend in \ref mainloop calling \c multiplex on unfinished and non-idle tasks.
   * Note that if the last call to \c multiplex takes considerable time then it is possible that the time spend
   * in \c mainloop will go arbitrarily far beyond \c sMaxDuration. It is the responsibility of the user to not
   * run states (of task) that can take too long in engines that have an \c maxDuration set.
   */
  static void setMaxDuration(float max_duration);
};
