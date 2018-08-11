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

#include "utils/Vector.h"
#include "utils/Singleton.h"
#include "debug.h"
#ifdef CWDEBUG
#include <libcwd/type_info.h>
#endif
#include <limits>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>

namespace statefultask {

#ifndef DOXYGEN
namespace ordering_category {
struct TimerQueue;	// Ordering category of TimerQueue;
} // namespace ordering_category
#endif

//! The type of an index into RunningTimers::m_queues.
using TimerQueueIndex = utils::VectorIndex<ordering_category::TimerQueue>;

class TimerQueue;
class RunningTimers;

#ifndef DOXYGEN
struct TimerTypes
{
  using clock_type = std::chrono::high_resolution_clock;
  using time_point = std::chrono::time_point<clock_type>;
};
#endif

template<TimerTypes::time_point::rep count, typename Unit>
struct Interval;

/*!
 * @brief A timer.
 *
 * Allows a callback to some <code>std::function<void()></code> that can
 * be specified during construction or while calling the \ref start member function.
 *
 * The \ref start member function must be passed an Interval object.
 */
struct Timer
{
  using clock_type = TimerTypes::clock_type;    //!< The underlaying clock type.
  using time_point = TimerTypes::time_point;    //!< The underlaying time point.

#if defined(CWDEBUG) && !defined(DOXYGEN)
  static bool s_interval_constructed;
#endif

#ifndef DOXYGEN
  // Use a value far in the future to represent 'no timer' (aka, a "timer" that will never expire).
  static time_point constexpr s_none{time_point::duration(std::numeric_limits<time_point::rep>::max())};

 public:
  /*!
   * @brief A timer interval.
   *
   * A Timer::Interval can only be instantiated from a <code>statefultask::Interval<count, Unit></code>
   * and only after main() is already reached. Normally you just want to pass a
   * <code>statefultask::Interval<count, Unit></code> directly to \ref start.
   */
  struct Interval
  {
   private:
    friend class Timer;         // Timer::start needs read access.
    TimerQueueIndex m_index;
    time_point::duration m_duration;

    template<TimerTypes::time_point::rep count, typename Unit>
    friend struct statefultask::Interval;
    Interval(TimerQueueIndex index_, time_point::duration duration_) : m_index(index_), m_duration(duration_) { Debug(Timer::s_interval_constructed = true); }
   public:
    Interval() { }

   public:
    //! A copy constructor is provided, but doesn't seem needed.
    Interval(Interval const& interval) : m_index(interval.m_index), m_duration(interval.m_duration) { Debug(Timer::s_interval_constructed |= !m_index.undefined()); }

    //! \internal For debugging purposes mainly.
    time_point::duration duration() { return m_duration; }
  };

  struct Handle
  {
    uint64_t m_sequence;        //!< A unique sequence number for Timer's with this interval. Only valid when running.
    TimerQueueIndex m_interval; //!< Interval index is_undefined means 'not running'.

    //! Default constructor. Construct a handle for a "not running timer".
    Handle() { }

    //! Construct a Handle for a running timer with interval \a interval and number sequence \a sequence.
    constexpr Handle(TimerQueueIndex interval, uint64_t sequence) : m_sequence(sequence), m_interval(interval) { }

    bool is_running() const { return !m_interval.undefined(); }
    void set_not_running() { m_interval.set_to_undefined(); }
    bool is_removed() const { return m_sequence == std::numeric_limits<uint64_t>::max(); }
    void set_removed() { m_sequence = std::numeric_limits<uint64_t>::max(); }
  };
#endif

 private:
  Handle m_handle;                      //!< If m_handle.is_running() returns true then this timer is running
                                        //   and m_handle can be used to find the corresponding Timer object.
  time_point m_expiration_point;        //!< The time at which we should expire (only valid when this is a running timer).
  std::function<void()> m_call_back;    //!< The callback function (only valid when this is a running timer).

 public:
  Timer() = default;
  Timer(std::function<void()> call_back) : m_call_back(call_back) { }

  //! Destruct the timer. If it is (still) running, stop it.
  ~Timer() { stop(); }

  //! Start this timer providing a (new) call back function.
  void start(Interval interval, std::function<void()> call_back, time_point now);

  //! Convenience function that calls clock_type::now() for you.
  void start(Interval interval, std::function<void()> call_back) { start(interval, call_back, clock_type::now()); }

  //! Start this timer using a previously assigned call back function.
  void start(Interval interval, time_point now);

  //! Convenience function that calls clock_type::now() for you.
  void start(Interval interval) { start(interval, clock_type::now()); }

  //! Stop this timer if it is (still) running.
  void stop();

  //! Called when this timer expires.
  void expire()
  {
    m_handle.set_not_running();
    m_call_back();
  }

  //! Called when this timer is removed from the queue.
  void removed()
  {
    m_handle.set_removed();
  }

  //! Call this to reset the call back function, destructing any possible objects that it might contain.
  void release_callback()
  {
    // Don't call release_callback while the timer is running.
    // You can call it from the call back itself however.
    ASSERT(!m_handle.is_running());
    m_call_back = std::function<void()>();
  }

  // Return the current time in the appropriate type.
  static time_point now() { return clock_type::now(); }

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

class Index;

class Indexes : public Singleton<Indexes>
{
  friend_Instance;

 private:
  Indexes() = default;
  ~Indexes() = default;
  Indexes(Indexes const&) = delete;

  std::multimap<Timer::time_point::rep, Index*> m_map;
  std::vector<Timer::time_point::rep> m_intervals;

 public:
  void add(Timer::time_point::rep period, Index* index);
  size_t number() const { return m_intervals.size(); }
  Timer::time_point::duration duration(int index) const { return Timer::time_point::duration{m_intervals[index]}; }
};

class Index
{
 private:
  friend class Indexes;
  std::size_t m_index;

 public:
  operator std::size_t() const { return m_index; }
};

template<Timer::time_point::rep period>
struct Register : public Index
{
  Register() { Indexes::instantiate().add(period, this); }
};

template<Timer::time_point::rep count, typename Unit>
struct Interval
{
  static constexpr Timer::time_point::rep period = std::chrono::duration_cast<Timer::time_point::duration>(Unit{count}).count();
  static Register<period> index;
  Interval() { }
  operator Timer::Interval() const { return {TimerQueueIndex(index), Timer::time_point::duration{period}}; }
};

//static
template<Timer::time_point::rep count, typename Unit>
constexpr Timer::time_point::rep Interval<count, Unit>::period;

//static
template<Timer::time_point::rep count, typename Unit>
Register<Interval<count, Unit>::period> Interval<count, Unit>::index;

} // namespace statefultask
