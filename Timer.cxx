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
#include <cassert>

namespace statefultask {

//static
Timer::time_point constexpr statefultask::Timer::s_none;

#ifdef CWDEBUG
//static
bool Timer::s_interval_constructed = false;
#endif

void Timer::start(Interval interval, std::function<void()> call_back, time_point now)
{
  // Call stop() first.
  ASSERT(!m_handle.is_running());
  m_expiration_point = now + interval.m_duration;
  m_call_back = call_back;
  m_handle = RunningTimers::instance().push(interval.m_index, this);
}

void Timer::start(Interval interval, time_point now)
{
  // Call stop() first.
  ASSERT(!m_handle.is_running());
  // Only use this on Timer objects that were constructed with a call back function.
  ASSERT(m_call_back);
  m_expiration_point = now + interval.m_duration;
  m_handle = RunningTimers::instance().push(interval.m_index, this);
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

void Indexes::add(Timer::time_point::rep period, Index* index)
{
  //DoutEntering(dc::notice, "Indexes::add(" << period << ", ...)");

  // Until all static objects are constructed, the index of an interval
  // can not be determined. Creating a Timer::Interval before reaching
  // main() is therefore doomed to have an incorrect 'index' field.
  //
  // The correct way to use Interval's by either instantiating global
  // variables of type Interval<count, Unit> and use those by name, or
  // simply pass them as a temporary to Timer::start directly.
  //
  // For example:
  //
  // statefultask::Interval<10, milliseconds> interval_10ms;
  //
  // And then use
  //
  //   timer.start(interval_10ms, ...);
  //
  // Or use it directly
  //
  //   timer.start(statefultask::Interval<10, milliseconds>(), ...);
  //
  // Do not contruct a Timer::Interval object before reaching main().
  ASSERT(!Timer::s_interval_constructed);

  m_intervals.resize(0);
  m_map.emplace(period, index);
  int in = -1;
  Timer::time_point::rep last = 0;
  for (auto i = m_map.begin(); i != m_map.end(); ++i)
  {
    if (i->first > last)
    {
      ++in;
      m_intervals.push_back(i->first);
    }
    i->second->m_index = in;
    last = i->first;
  }
  RunningTimers::instantiate().initialize(m_intervals.size());
}

namespace {
SingletonInstance<Indexes> dummy __attribute__ ((__unused__));
} // namespace

} // namespace statefultask
