/**
 * ai-statefultask -- Asynchronous, Stateful Task Scheduler library.
 *
 * @file
 * @brief Implementation of AIEngine.
 *
 * @Copyright (C) 2010 - 2013, 2017  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This file is part of ai-statefultask.
 *
 * Ai-statefultask is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Ai-statefultask is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with ai-statefultask.  If not, see <http://www.gnu.org/licenses/>.
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
  engine_state_w->list.emplace_back(stateful_task);
  if (engine_state_w->waiting)
  {
    engine_state_w->waiting = false;
    engine_state_w.notify_one();
  }
}

// Run tasks in this engine, more or less in the order of from low to high previous execution time,
// until each task did run at most once (until idle or finish/abort) or until the time in mMaxDuration
// was succeeded.
//
// This function returns whether or not there are tasks left that need more CPU, either because
// they did yield, or because the mMaxDuration time-limit was reached.
//
// It returns exclusively fuzzy::WasFalse (because the instant this function returns the engine is
// unlocked and new tasks could be added) or fuzzy::True (because this function must be externally
// synchronized; only called by one thread at a time-- and tasks are only removed in this function.
// Hence the calling thread can rely on it that a true value stays true).
utils::FuzzyBool AIEngine::mainloop()
{
  queued_type::iterator queued_element, end;
  {
    // Lock engine and initialize begin (queued_element) and end.
    engine_state_type::wat engine_state_w(mEngineState);
    queued_element = engine_state_w->list.begin();
    end = engine_state_w->list.end();
    // Is there anything to do?
    if (queued_element == end)
    {
      // Nothing to do. Wait till something is added to the queue again.
      if (!mHasMaxDuration)
      {
        // We don't care how long this engine stays in the mainloop().
        // Just sleep here until a new task is added.
        engine_state_w->waiting = true;
        engine_state_w.wait([&](){ return !engine_state_w->waiting; });
        // If waiting was reset, then a task was added, so call mainloop again.
        return fuzzy::True;
      }
      // There is no need to call mainloop again until there is another task added.
      // Note that tasks should be added first, BEFORE setting a flag indicating that mainloop() needs to be called again.
      return fuzzy::WasFalse;
    }
  }
  // There is at least one non-idle task that needs to run in this engine.
  // More tasks can be added here (mEngineState is no longer locked), but not removed.
  // It is possible that a signal() is called on one of the tasks, but that would
  // never cause it to run (outside of this engine).
  duration_type total_duration(duration_type::zero());
  bool one_or_more_tasks_called_yield = false;
  bool only_task = false;
  // This loop runs over all active tasks once (possible new ones too that were added at the end while executing this loop).
  do
  {
    // Iterators to the list are not invalidated by insertion or deletion (of other elements).
    AIStatefulTask& stateful_task(queued_element->stateful_task());
    if (mHasMaxDuration)
    {
      // Keep track of the time that each task did run, as well as the total duration of this mainloop() invokation.
      clock_type::time_point start = clock_type::now();
      // Skip a task if it is sleeping (or skipping frames), except when this is the last/only task left
      // after already running other tasks and that last task is just yielding milliseconds (not frames).
      if (!stateful_task.sleep(start, only_task))
      {
        // This runs a task until it is idle or finished.
        stateful_task.multiplex(AIStatefulTask::normal_run, this);
      }
      clock_type::duration delta = clock_type::now() - start;
      stateful_task.add(delta);
      total_duration += delta;
    }
    else
    {
      // Just run until this task is idle or finished.
      stateful_task.multiplex(AIStatefulTask::normal_run, this);
    }

    // Still running in this engine?
    bool active = stateful_task.active(this);   // This locks mState shortly, so it must be called before locking mEngineState because add() locks mEngineState while holding mState.
    engine_state_type::wat engine_state_w(mEngineState);
    // Note: new elements can have been added to the end of the list; so it is possible that we encounter them here when incrementing queued_element.
    if (!active)
    {
      Dout(dc::statefultask(stateful_task.mSMDebug), "Erasing stateful task [" << (void*)&stateful_task << "] from engine \"" << mName << "\".");
      engine_state_w->list.erase(queued_element++);
    }
    else
    {
      ++queued_element;
      // The only reason to return from multiplex while still active is when yield() was called.
      one_or_more_tasks_called_yield = true;
    }
    if (mHasMaxDuration)
    {
      size_t number_of_tasks_left = engine_state_w->list.size();
      only_task = number_of_tasks_left == 1;                            // This is kinda fuzzy. As soon as engine_state_w is destructed more tasks could be added.
      if (total_duration >= mMaxDuration)
      {
        // It's taking too long. Leave mainloop().
        if (number_of_tasks_left > 2)
        {
          Dout(dc::statefultask, "Sorting " << engine_state_w->list.size() << " stateful tasks.");
          engine_state_w->list.sort(QueueElementComp());
        }
        // Return true if there are remaining tasks.
        return (one_or_more_tasks_called_yield || queued_element != end) ? fuzzy::True : fuzzy::WasFalse;
      }
    }
  }
  while (queued_element != end);
  // Return true if there are remaining tasks.
  return one_or_more_tasks_called_yield ? fuzzy::True : fuzzy::WasFalse;
}

void AIEngine::flush()
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
void AIEngine::setMaxDuration(float max_duration)
{
  mHasMaxDuration = max_duration > 0.0f;
  if (mHasMaxDuration)
  {
    Dout(dc::statefultask, "(Re)calculating AIEngine::mMaxDuration");
    mMaxDuration = std::chrono::duration_cast<duration_type>(std::chrono::duration<float, std::milli>(max_duration));
  }
}

void AIEngine::wake_up()
{
  engine_state_type::wat engine_state_w(mEngineState);
  if (engine_state_w->waiting)
  {
    engine_state_w->waiting = false;
    engine_state_w.notify_one();
  }
}

bool AIEngine::QueueElementComp::operator()(QueueElement const& e1, QueueElement const& e2) const
{
  return e1.mStatefulTask->getDuration() < e2.mStatefulTask->getDuration();
}
