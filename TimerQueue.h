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
 private:
  uint64_t m_sequence_offset;                   // The number of timers that were popped from m_running_timers.
  std::deque<Timer*> m_running_timers;          // All running timers for the related interval.

 public:
  //! Construct an empty queue.
  TimerQueue() : m_sequence_offset(0) { }

  /*!
   * @brief Add a new timer to the end of the queue.
   *
   * @returns An ever increasing sequence number starting with 0.
   */
  uint64_t push(Timer* timer)
  {
    m_running_timers.emplace_back(timer);
    return m_running_timers.size() - 1 + m_sequence_offset;
  }

  /*!
   * @brief Cancelled a running timer.
   *
   * The \a sequence passed must be returned by a previous call to push().
   *
   * @returns True if the cancelled Timer is the current timer.
   */
  bool cancel(uint64_t sequence)
  {
    size_t i = sequence - m_sequence_offset;
    // Sequence must be returned by a previous call to push() and the Timer may not already have expired.
    ASSERT(0 <= i && i < m_running_timers.size());
    // Do not cancel a timer twice.
    ASSERT(m_running_timers[i]);
    m_running_timers[i] = nullptr;
    return i == 0;
  }

  /*!
   * @brief Remove one timer from the front of the queue and return it.
   *
   * This function may only be called when the queue is not empty.
   *
   * @returns The current timer. Might be nullptr if the current timer was cancelled.
   */
  Timer* pop()
  {
    // Do not call pop() when the queue is empty.
    ASSERT(!m_running_timers.empty());
    Timer* running_timer{m_running_timers.front()};
    ++m_sequence_offset;
    m_running_timers.pop_front();
    return running_timer;
  }

  void pop_cancelled_timers()
  {
    // Do not call pop_cancelled_timers() when the queue is empty.
    ASSERT(!m_running_timers.empty());
    // Only call remove_cancelled_timers() after cancel() returned true;
    ASSERT(!m_running_timers.front());
    do
    {
      ++m_sequence_offset;
      m_running_timers.pop_front();
    }
    while (!m_running_timers.empty() && m_running_timers.front() == nullptr);       // Is the next timer also cancelled?
  }

  /*!
   * @brief Return the next time point at which a timer of this interval will expire.
   */
  Timer::time_point front() const
  {
    if (m_running_timers.empty())
      return Timer::none;
    return m_running_timers.front()->get_expiration_point();
  }

  // Return true if \a sequence is the value returned by a call to push() for
  // a timer that is now at the front (will be returned by pop()).
  bool is_current(uint64_t sequence) const { return sequence == m_sequence_offset; }

  // Return the expiration point for the related interval that will expire next.
  Timer::time_point next_expiration_point()
  {
    while (!m_running_timers.empty())
    {
      Timer::Timer* timer = m_running_timers.front();
      if (timer)
        return timer->get_expiration_point();
      ++m_sequence_offset;
      m_running_timers.pop_front();
    }
    return Timer::none;
  }

  // Return true if are no running timers for the related interval.
  bool empty() const { return m_running_timers.empty(); }

  // Only used for testing.
  size_t size() const { return m_running_timers.size(); }

  int cancelled_in_queue() const
  {
    int sz = 0;
    for (auto timer : m_running_timers)
      sz += timer ? 0 : 1;
    return sz;
  }

  uint64_t get_sequence_offset() const { return m_sequence_offset; }

  auto begin() const { return m_running_timers.begin(); }
  auto end() const { return m_running_timers.end(); }

};

} // namespace statefultask
