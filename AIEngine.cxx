/**
 * @file
 * @brief Implementation of AIEngine.
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

#include "sys.h"
#include "AIEngine.h"

void AIEngine::add(AIStatefulTask* stateful_task)
{
  Dout(dc::statefultask(stateful_task->mSMDebug), "Adding stateful task [" << (void*)stateful_task << "] to " << mName);
  engine_state_type::wat engine_state_w(mEngineState);
  engine_state_w->list.push_back(QueueElement(stateful_task));
  if (engine_state_w->waiting)
  {
    engine_state_w.signal();
  }
}

// MAIN-THREAD
void AIEngine::mainloop(void)
{
  queued_type::iterator queued_element, end;
  {
    engine_state_type::wat engine_state_w(mEngineState);
    end = engine_state_w->list.end();
    queued_element = engine_state_w->list.begin();
  }
  duration_type total_duration(duration_type::zero());
  while (queued_element != end)
  {
    AIStatefulTask& stateful_task(queued_element->stateful_task());
    clock_type::time_point start = clock_type::now();
    if (!stateful_task.sleep(start))
    {
      stateful_task.multiplex(AIStatefulTask::normal_run);
    }
    clock_type::duration delta = clock_type::now() - start;
    stateful_task.add(delta);
    total_duration += delta;

    bool active = stateful_task.active(this);   // This locks mState shortly, so it must be called before locking mEngineState because add() locks mEngineState while holding mState.
    engine_state_type::wat engine_state_w(mEngineState);
    if (!active)
    {
      Dout(dc::statefultask(stateful_task.mSMDebug), "Erasing stateful task [" << (void*)&stateful_task << "] from " << mName);
      engine_state_w->list.erase(queued_element++);
    }
    else
    {
      ++queued_element;
    }
    if (total_duration >= sMaxDuration)
    {
      Dout(dc::statefultask, "Sorting " << engine_state_w->list.size() << " stateful tasks.");
      engine_state_w->list.sort(QueueElementComp());
      break;
    }
  }
}

void AIEngine::flush(void)
{
  engine_state_type::wat engine_state_w(mEngineState);
  DoutEntering(dc::statefultask, "AIEngine::flush [" << mName << "]: calling force_killed() on " << engine_state_w->list.size() << " stateful tasks.");
  for (queued_type::iterator iter = engine_state_w->list.begin(); iter != engine_state_w->list.end(); ++iter)
  {
    // To avoid an assertion in ~AIStatefulTask.
    iter->stateful_task().force_killed();
  }
  engine_state_w->list.clear();
}

// static
AIEngine::duration_type AIEngine::sMaxDuration;

// static
void AIEngine::setMaxDuration(float max_duration)
{
  ASSERT(aithreadid::in_main_thread());
  Dout(dc::statefultask, "(Re)calculating AIEngine::sMaxDuration");
  sMaxDuration = std::chrono::duration_cast<duration_type>(std::chrono::duration<float, std::milli>(max_duration));
}

AIEngine gMainThreadEngine("gMainThreadEngine");
AIEngine gAuxiliaryThreadEngine("gAuxiliaryThreadEngine");

// Auxiliary Thread main loop.
void AIEngine::threadloop(void)
{
  queued_type::iterator queued_element, end;
  {
    engine_state_type::wat engine_state_w(mEngineState);
    end = engine_state_w->list.end();
    queued_element = engine_state_w->list.begin();
    if (queued_element == end)
    {
      // Nothing to do. Wait till something is added to the queue again.
      engine_state_w->waiting = true;
      engine_state_w.wait();
      engine_state_w->waiting = false;
      return;
    }
  }
  do
  {
    AIStatefulTask& stateful_task(queued_element->stateful_task());
    stateful_task.multiplex(AIStatefulTask::normal_run);
    bool active = stateful_task.active(this);           // This locks mState shortly, so it must be called before locking mEngineState because add() locks mEngineState while holding mState.
    engine_state_type::wat engine_state_w(mEngineState);
    if (!active)
    {
      Dout(dc::statefultask(stateful_task.mSMDebug), "Erasing stateful task [" << (void*)&stateful_task << "] from " << mName);
      engine_state_w->list.erase(queued_element++);
    }
    else
    {
      ++queued_element;
    }
  }
  while (queued_element != end);
}

void AIEngine::wake_up(void)
{
  engine_state_type::wat engine_state_w(mEngineState);
  if (engine_state_w->waiting)
  {
    engine_state_w.signal();
  }
}

bool AIEngine::QueueElementComp::operator()(QueueElement const& e1, QueueElement const& e2) const
{
  return e1.mStatefulTask->getDuration() < e2.mStatefulTask->getDuration();
}
