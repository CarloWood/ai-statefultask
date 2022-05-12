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

void AITimer::expired()
{
  mHasExpired.store(true, std::memory_order_relaxed);
  signal(1);
}

void AITimer::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case AITimer_start:
      {
        mTimer.start(mInterval);
	wait_until([&]{ return mHasExpired.load(std::memory_order_relaxed); }, 1, AITimer_expired);
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
