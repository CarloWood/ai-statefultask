/**
 * @file
 * @brief Declaration of class TimerQueue.
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

#include <deque>
#include <cstdint>
#include "Timer.h"
#include "debug.h"

namespace statefultask {

class RunningTimers;

/*!
 * @brief A queue of running (possibly cancelled) timers, all of the same interval.
 *
 * This queue stores Timer*'s. Each Timer will have the same interval (which interval
 * that is depends on the context in which the TimerQueue was found). If a pointer
 * is nullptr then it represents a cancelled timer; such timers are not removed
 * from the queue because that would cost too much CPU.
 *
 * In the description of the member functions, 'current' means the next timer
 * that will be returned by pop(), also if that timer was already cancelled!
 */
class TimerQueue
{
  using running_timers_type = std::deque<Timer*>;

 private:
  uint64_t m_sequence_offset;                   // The number of timers that were popped from m_running_timers.
  running_timers_type m_running_timers;         // All running timers for the related interval.

 public:
  //! Construct an empty queue.
  TimerQueue() : m_sequence_offset(0) { }

  /*!
   * @brief Add a new timer to the end of the queue.
   *
   * The TimerQueue must be locked before calling push(), and
   * without releasing that lock the result should be passed
   * to is_current() to check if m_running_timers was empty.
   * If that is the case then RunningTimers::m_mutex must
   * be locked before releasing the lock on this queue.
   * timer->get_expiration_point() can subsequently be
   * used to update RunningTimers::m_cache.
   *
   * @returns An ever increasing sequence number starting with 0.
   */
  uint64_t push(Timer* timer)
  {
    uint64_t size = m_running_timers.size();
    m_running_timers.emplace_back(timer);
    return size + m_sequence_offset;
  }

  /*!
   * @brief Check if a timer is current.
   *
   * This function might need to be called after calling push, in order
   * to check if a newly added timer expires sooner than what we're
   * currently waiting for.
   *
   * @returns True if \a sequence is the value returned by a call to push() for a timer that is now at the front (will be returned by pop() next).
   */
  bool is_current(uint64_t sequence) const
  {
    return sequence == m_sequence_offset;
  }

  /*!
   * @brief Cancel a running timer.
   *
   * The \a sequence passed must be returned by a previous call to push() and may not have expired.
   * This implies that the queue cannot be empty.
   *
   * If this function returns true then the mutex \a m has been locked
   * and RunningTimers needs updating.
   *
   * @param sequence : The sequence number of a previously pushed timer.
   * @param m : A reference to RunningTimers::m_mutex.
   *
   * @returns True if the cancelled Timer was the current timer.
   */
  bool cancel(uint64_t sequence, std::mutex& m)
  {
    uint64_t index = sequence - m_sequence_offset;
    // Sequence must be returned by a previous call to push() and the Timer may not already have expired.
    ASSERT(index < m_running_timers.size());
    // Do not cancel a timer twice.
    ASSERT(m_running_timers[index]);
    bool is_current = index == 0;
    if (is_current)
      pop(m);
    else
      m_running_timers[index] = nullptr;
    return is_current;
  }

  /*!
   * @brief Return the timer at the front of the queue.
   *
   * This function may only be called when the queue is not empty.
   * RunningTimers::m_mutex must be locked before calling this function.
   * The returned pointer will never be null.
   *
   * @returns The current timer.
   */
  Timer* peek()
  {
    // Do not call peek() when the queue is empty.
    ASSERT(!m_running_timers.empty());
    return m_running_timers.front();
  }

  /*!
   * @brief Remove one timer from the front of the queue and return it.
   *
   * This function may only be called when the queue is not empty.
   * The returned pointer will never be null.
   *
   * Afterwards, the mutex \a m is locked.
   *
   * @param m : A reference to RunningTimers::m_mutex.
   *
   * @returns The current timer.
   */
  Timer* pop(std::mutex& m)
  {
    running_timers_type::iterator b = m_running_timers.begin();
    running_timers_type::iterator const e = m_running_timers.end();

    // Do not call pop() when the queue is empty.
    ASSERT(b != e);

    Timer* timer = *b;

    do
    {
      ++m_sequence_offset;
      ++b;
    }
    while (b != e && *b == nullptr);   // Is the next timer cancelled?

    // Mark this timer as being removed.
    timer->removed();

    // Erase the range [begin, b).
    m.lock();
    m_running_timers.erase(m_running_timers.begin(), b);

    return timer;
  }

  /*!
   * @brief Return the next time point at which a timer of this interval will expire.
   *
   * This function returns Timer::none if the queue is empty.
   * This makes it suitable to be passed to increase_cache.
   *
   * RunningTimers::m_mutex must be locked before calling this function.
   * The returned expiration point can be used to update RunningTimers::m_cache
   * while keeping m_mutex locked.
   */
  Timer::time_point next_expiration_point() const
  {
    if (m_running_timers.empty())
      return Timer::s_none;
    // Note that front() is never a cancelled timer.
    return m_running_timers.front()->get_expiration_point();
  }

 private:
  friend class RunningTimers;
  void set_not_running()
  {
    for (Timer* timer : m_running_timers)
      if (timer)
        timer->set_not_running();
  }

 public:
  //--------------------------------------------------------------------------
  // Everything below is just for debugging.

  // Return true if there are no running timers for the related interval.
  bool debug_empty() const { return m_running_timers.empty(); }

  // Return the number of elements in m_running_timers. This includes cancelled timers.
  running_timers_type::size_type debug_size() const { return m_running_timers.size(); }

  // Return the number of element in m_running_timers that are cancelled.
  int debug_cancelled_in_queue() const
  {
    int sz = 0;
    for (auto timer : m_running_timers)
      sz += timer ? 0 : 1;
    return sz;
  }

  // Accessor for m_sequence_offset.
  uint64_t debug_get_sequence_offset() const { return m_sequence_offset; }

  // Allow iterating directly over all elements of m_running_timers.
  auto debug_begin() const { return m_running_timers.begin(); }
  auto debug_end() const { return m_running_timers.end(); }
};

} // namespace statefultask
