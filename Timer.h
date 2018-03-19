/**
 * @file
 * @brief Declaration of class Timer.
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

#pragma once

#include <limits>
#include <chrono>
#include <cstdint>
#include <functional>
#include "utils/Array.h"

namespace statefultask {

#ifndef DOXYGEN
namespace ordering_category {
struct TimerQueue;	// Ordering category of TimerQueue;
} // namespace ordering_category
#endif

//! The type of an index for RunningTimers::m_queues.
using TimerQueueIndex = utils::ArrayIndex<ordering_category::TimerQueue>;

class TimerQueue;

/*!
 * @brief A timer.
 */

struct Timer
{
  using clock_type = std::chrono::high_resolution_clock;
  using time_point = std::chrono::time_point<clock_type>;

  // Use a value far in the future to represent 'no timer' (aka, a "timer" that will never expire).
  static time_point constexpr none{time_point::duration(std::numeric_limits<time_point::rep>::max())};

  struct Interval
  {
    TimerQueueIndex index;
    time_point::duration duration;
    Interval() { }
    Interval(TimerQueueIndex index_, time_point::duration duration_) : index(index_), duration(duration_) { }
    Interval(Interval const& interval) : index(interval.index), duration(interval.duration) { }
  };

  struct Handle
  {
    uint64_t m_sequence;        //!< A unique sequence number for Timer's with this interval. Only valid when running.
    TimerQueueIndex m_interval; //!< Interval index is_undefined means 'not running'.

    //! Default constructor. Construct a handle for a "not running timer".
    Handle() { }

    //! Construct a Handle for a running timer with interval \a interval and number sequence \a sequence.
    constexpr Handle(TimerQueueIndex interval, uint64_t sequence) : m_sequence(sequence), m_interval(interval) { }

    //! 
    bool is_running() const { return !m_interval.undefined(); }
    void set_not_running() { m_interval.set_to_undefined(); }
  };

  Handle m_handle;                      //!< If m_handle.is_running() returns true then this timer is running
                                        //   and m_handle can be used to find the corresponding Timer object.
  time_point m_expiration_point;        //!< The time at which we should expire (only valid when this is a running timer).
  std::function<void()> m_call_back;    //!< The callback function (only valid when this is a running timer).

  //! Destruct the timer. If it is (still) running, stop it.
  ~Timer() { stop(); }

  //! Start this timer.
  void start(Interval interval, std::function<void()> call_back, time_point now);

  //! Stop this timer if it is (still) running.
  void stop();

  //! Called when this timer expires.
  void expire()
  {
    m_handle.set_not_running();
    m_call_back();
  }

 private:
  friend class TimerQueue;
  // Called by RunningTimers upon destruction. Causes a later call to stop() not to access RunningTimers anymore.
  void set_not_running();

 public:
  // Accessors.

  //! Return the handle of this timer.
  Handle handle() const { return m_handle; }

  //! Return the point at which this timer will expire. Only valid when is_running.
  time_point get_expiration_point() const { return m_expiration_point; }
};

} // namespace statefultask
