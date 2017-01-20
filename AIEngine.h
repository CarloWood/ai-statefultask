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

        AIStatefulTask const& stateful_task(void) const { return *mStatefulTask; }
        AIStatefulTask& stateful_task(void) { return *mStatefulTask; }
    };
    struct QueueElementComp {
      inline bool operator()(QueueElement const& e1, QueueElement const& e2) const;
    };

  public:
    typedef std::list<QueueElement> queued_type;
    struct engine_state_st {
      queued_type list;
      bool waiting;
      engine_state_st(void) : waiting(false) { }
    };

    typedef AIStatefulTask::clock_type clock_type;
    typedef AIStatefulTask::duration_type duration_type;

  private:
    typedef aithreadsafe::Wrapper<engine_state_st, aithreadsafe::policy::Primitive<aithreadsafe::Condition>> engine_state_type;
    engine_state_type mEngineState;
    char const* mName;
    static duration_type sMaxDuration;

  public:
    AIEngine(char const* name) : mName(name) { }

    void add(AIStatefulTask* stateful_task);

    void mainloop(void);
    void threadloop(void);
    void wake_up(void);
    void flush(void);

    char const* name(void) const { return mName; }

    static void setMaxDuration(float max_duration);
};
