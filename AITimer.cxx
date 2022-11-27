/**
 * ai-statefultask -- Asynchronous, Stateful Task Scheduler library.
 *
 * @file
 * @brief Implementation of AITimer.
 *
 * @Copyright (C) 2012, 2017  Carlo Wood.
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
 *   07/02/2012
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#include "sys.h"
#include "AITimer.h"

char const* AITimer::state_str_impl(state_type run_state) const
{
  switch(run_state)
  {
    AI_CASE_RETURN(AITimer_start);
    AI_CASE_RETURN(AITimer_expired);
  }
  AI_NEVER_REACHED;
}

char const* AITimer::task_name_impl() const
{
  return "AITimer";
}

void AITimer::expired()
{
  mHasExpired.store(true, std::memory_order_relaxed);
  // While expiring we have the mutex mTimer::m_calling_expire locked to prevent destruction while
  // expiring; however - in immediate mode, if there is only a single reference count left, the
  // call to signal(1) will cause a destruct and thus a dead-lock.
  //
  // It seems unsafe to allow destruction here while this can also be solved by keeping
  // a reference count for each timer:
  //
  // Have a boost::intrusive_ptr<AITimer> m_timer; with a sufficient long life-time,
  // create it the usual way with:
  //
  //   m_timer = statefultask::create<AITimer>(CWDEBUG_ONLY(true));
  //   m_timer->set_interval(threadpool::Interval<2, std::chrono::seconds>());
  //   m_timer->run([this](bool success){ do_something(); });
  //
  // In order to be sure that the object that do_something is called on
  // still exists, then destructor of this object must call m_timer->abort()
  // from its destructor! And in order to be able to call abort() one must
  // have a boost::intrusive_ptr anyway.
  //
  // Or, don't use an immediate handler for the timer.
  ASSERT(!default_is_immediate() || unique().is_momentary_false());
  signal(timer_expired_condition);
}

void AITimer::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case AITimer_start:
      {
        // In case the timer was restarted.
        mHasExpired = false;
        mTimer.start(mInterval);
	wait_until([&]{ return mHasExpired.load(std::memory_order_relaxed); }, timer_expired_condition, AITimer_expired);
        break;
      }
    case AITimer_expired:
      {
        finish();
        break;
      }
  }
}

void AITimer::abort_impl()
{
  mTimer.stop();
}

char const* AITimer::condition_str_impl(condition_type condition) const
{
  switch (condition)
  {
    AI_CASE_RETURN(timer_expired_condition);
  }
  return direct_base_type::condition_str_impl(condition);
}
