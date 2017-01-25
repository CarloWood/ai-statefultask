/**
 * @file
 * @brief Implementation of AICondition
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

#include "sys.h"
#include "AICondition.h"
#include "AIStatefulTask.h"

// Constructor and destructor need "AIStatefulTask.h" for member mWaitingStatefulTasks.
AIConditionBase::AIConditionBase()
{
}

AIConditionBase::~AIConditionBase()
{
}

void AIConditionBase::wait(AIStatefulTask* stateful_task)
{
  // The condition must be locked before calling AIStatefulTask::wait().
  ASSERT(mutex().self_locked());
  // Add the new task at the end.
  mWaitingStatefulTasks.push_back(stateful_task);
}

void AIConditionBase::remove(AIStatefulTask* stateful_task)
{
  mutex().lock();
  // Remove all occurances of stateful_task from the queue.
  queue_type::iterator const end = mWaitingStatefulTasks.end();
  queue_type::iterator last = end;
  for (queue_type::iterator iter = mWaitingStatefulTasks.begin(); iter != last; ++iter)
  {
    if (iter->get() == stateful_task)
    {
      if (--last == iter)
      {
        break;
      }
      iter->swap(*last);
    }
  }
  // This invalidates all iterators involved, including end, but not any iterators to the remaining elements.
  mWaitingStatefulTasks.erase(last, end);
  mutex().unlock();
}

void AIConditionBase::signal(int n)
{
  // The condition must be locked before calling AICondition::signal or AICondition::broadcast.
  ASSERT(mutex().self_locked());
  // Signal n tasks.
  while (n > 0 && !mWaitingStatefulTasks.empty())
  {
    boost::intrusive_ptr<AIStatefulTask> stateful_task = mWaitingStatefulTasks.front();
    bool success = stateful_task->signalled();
    // Only tasks that are actually still blocked should be in the queue:
    // they are removed from the queue by calling AICondition::remove whenever
    // they are unblocked for whatever reason...
    ASSERT(success);
    if (success)
    {
      ++n;
    }
    else
    {
      // We never get here...
      remove(stateful_task.get());
    }
  }
}

