/**
 * @file
 * @brief Implementation of Timer.
 *
 * @Copyright (C) 2018 Carlo Wood.
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
 */

#include "sys.h"
#include "debug.h"
#include "Timer.h"
#include "RunningTimers.h"

namespace statefultask {

//static
Timer::time_point constexpr statefultask::Timer::none;

void Timer::start(Interval interval, std::function<void()> call_back, time_point now)
{
  // Call stop() first.
  ASSERT(!m_handle.is_running());
  m_expiration_point = now + interval.duration;
  m_call_back = call_back;
  m_handle = RunningTimers::instance().push(interval.index, this);
}

void Timer::stop()
{
  if (m_handle.is_running())
  {
    RunningTimers::instance().cancel(m_handle);
    m_handle.set_not_running();
  }
}

void Timer::set_not_running()
{
  m_handle.set_not_running();
}

} // namespace statefultask
