/**
 * @file
 * @brief Declaration of class RunningTimers.
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

#include "utils/nearest_power_of_two.h"
#include "utils/Singleton.h"
#include "TimerQueue.h"
#include "debug.h"
#include <array>
#include <csignal>
#include <mutex>

namespace statefultask {

// This is a tournament tree of queues of timers with the same interval.
//
// The advantage of using queues of timers with the same interval
// is that it is extremely cheap to insert a new timer with a
// given interval when there is already another timer with the
// same interval running. On top of that, it is likely that even
// a program that has a million timers running simultaneously
// only uses a handful of distinct intervals, so the size of
// the tree/heap shrinks dramatically.
//
// Assume the number of different intervals is 6, and thus tree_size == 8,
// then the structure of the data in RunningTimers could look like:
//
// Tournament tree (tree index:tree index)
// m_tree:                                              1:4
//                                             /                  \                             .
//                                  2:0                                     3:4
//                              /         \                             /         \             .
//                        4:0                 5:3                 6:4                 7:6
//                      /     \             /     \             /     \             /     \     .
// m_cache:           18  no_timer       102        55        10        60  no_timer  no_timer
// index of m_cache:   0         1         2         3         4         5         6         7
//
// Thus, m_tree[1] contains 4, m_tree[2] contains 0, m_tree[3] contains 4 and so on.
// Each time a parent contains the same interval (in) as one of its children and well
// such that m_cache[in] contains the smallest time_point value of the two.
// m_cache[in] contains a copy of the top of m_queues[in].
//
class RunningTimers : public Singleton<RunningTimers>
{
  friend_Instance;
 private:
  RunningTimers();
  ~RunningTimers();
  RunningTimers(RunningTimers const&) = delete;

 protected:
  static int constexpr tree_size = 64;                                  // Allow at most 64 different time intervals (so that m_tree fits in a single cache line).

  std::mutex m_mutex;                                                   // Protects (the consistency of) m_tree and m_cache.
  std::array<uint8_t, tree_size> m_tree;
  std::array<Timer::time_point, tree_size> m_cache;
  utils::Vector<TimerQueue, TimerQueueIndex> m_queues;
  timer_t m_timer;
  sigset_t m_sigalrm_set;
  sigset_t m_prev_sigset;

  static int constexpr parent_of(int index)                             // Used in increase_cache and decrease_cache.
  {
    return index >> 1;
  }

  static int constexpr interval_to_parent_index(int in)                 // Used in increase_cache and decrease_cache.
  {
    return (in + tree_size) >> 1;
  }

  static int constexpr sibling_of(int index)                            // Used in increase_cache.
  {
    return index ^ 1;
  }

  static int constexpr left_child_of(int index)                         // Only used in constructor.
  {
    return index << 1;
  }

  //=====================================================================================
  // DANGER: do not change the two functions below! They are extremely sensitive to bugs!
  // They were created by trial and error in a testsuite that brute force tested all 1500
  // possibilities. If you make any change you WILL break it.
  //=====================================================================================
  //
  void increase_cache(int interval, Timer::time_point tp)       // m_mutex must be locked.
  {
    ASSERT(tp >= m_cache[interval]);
    m_cache[interval] = tp;

    int parent_ti = interval_to_parent_index(interval); // Let 'parent_ti' be the index of the parent node in the tree above 'interval'.

    int in = interval;                                  // Let 'in' be the interval whose value is changed with respect to m_tree[parent_ti].
    int si = in ^ 1;                                    // Let 'si' be the interval of the current sibling of in.
    for(;;)
    {
      Timer::time_point sv = m_cache[si];
      if (tp > sv)
      {
        if (m_tree[parent_ti] == si)
          break;
        tp = sv;
        in = si;
      }
      m_tree[parent_ti] = in;                           // Update the tree.
      if (parent_ti == 1)                               // If this was the top-most node in the tree then we're done.
        break;
      si = m_tree[sibling_of(parent_ti)];               // Update the sibling interval.
      parent_ti = parent_of(parent_ti);                 // Set 'parent_ti' to be the index of the parent node in the tree above 'parent_ti'.
    }
  }

  void decrease_cache(int interval, Timer::time_point tp)       // m_mutex must be locked.
  {
    ASSERT(tp <= m_cache[interval]);
    m_cache[interval] = tp;                             // Replace no_timer with tp.
    // We just put a SMALLER value in the cache at position interval than what there was before.
    // Therefore all we have to do is overwrite parents with our interval until the time_point
    // value of the parent is less than tp.
    int parent_ti = interval_to_parent_index(interval); // Let 'parent_ti' be the index of the parent node in the tree above 'interval'.
    while (tp <= m_cache[m_tree[parent_ti]])            // m_tree[parent_ti] is the content of that node. m_cache[m_tree[parent_ti]] is the value.
    {
      m_tree[parent_ti] = interval;                     // Update that tree node.
      if (parent_ti == 1)                               // If this was the top-most node in the tree then we're done.
        break;
      parent_ti = parent_of(parent_ti);                 // Set 'i' to be the index of the parent node in the tree above 'i'.
    }
  }
  //=====================================================================================

 public:
  // Returns the first expired Timer, if any. Also sets the hardware timer to raise a signal
  // when the next Timer will expire when there isn't an already expired Timer right now.
  // If a non-null value is returned, call Timer::expire() on it.
  Timer* next_expired(Timer::time_point now);

  // Wait for a signal and returns true if one was caught.
  void wait_for_expire()
  {
    // Put this thread to sleep until a (timer) signal is received.
    Dout(dc::notice, "wait_for_expire(): calling sigsuspend.");
    sigdelset(&m_prev_sigset, SIGALRM); // Make sure we wake up on pending SIGALRM signals.
    sigsuspend(&m_prev_sigset);
    ASSERT(errno == EINTR);
    Dout(dc::notice, "wait_for_expire(): returning from sigsuspend.");
  }

  int to_cache_index(TimerQueueIndex index) const { return index.get_value(); }
  TimerQueueIndex to_queues_index(int index) const { return TimerQueueIndex(index); }

  //! Cancel the timer associated with handle.
  void cancel(Timer::Handle const& handle)
  {
    TimerQueue& queue{m_queues[handle.m_interval]};

    if (!queue.cancel(handle.m_sequence))       // Not the current timer for this interval?
      return;                                   // Then not the current timer.

    int cache_index = to_cache_index(handle.m_interval);
    Timer::time_point expiration_point = queue.next_expiration_point();

    std::unique_lock<std::mutex> lk(m_mutex);
    bool is_current = m_tree[1] == cache_index;
    increase_cache(cache_index, expiration_point);

    // Call update_running_timer if the cancelled timer is the currently running timer.
    if (is_current)
      update_running_timer();
  }

  // Add \a timer to the list of running timers, using \a interval as timeout.
  Timer::Handle push(TimerQueueIndex interval, Timer* timer)
  {
    assert(interval.get_value() < m_queues.size());
    uint64_t sequence = m_queues[interval].push(timer);
    bool const is_current = m_queues[interval].is_current(sequence);
    Timer::Handle handle{interval, sequence};
    if (is_current)
    {
      int cache_index = to_cache_index(interval);
      Timer::time_point expiration_point = timer->get_expiration_point();
      std::unique_lock<std::mutex> lk(m_mutex);
      decrease_cache(cache_index, expiration_point);
      if (m_tree[1] == cache_index)
        update_running_timer();
    }
    return handle;
  }

  void initialize(size_t number_of_intervals)
  {
    ASSERT(number_of_intervals <= (size_t)tree_size);
    // No need to lock m_mutex here because initialize this only called before we even reached main().
    m_queues.resize(number_of_intervals);
  }

 private:
  void update_running_timer()
  {
  }

#ifdef CWDEBUG
  //--------------------------------------------------------------------------
  // Everything below is just for debugging.

  size_t debug_size() const
  {
    size_t sz = 0;
    for (auto&& queue : m_queues)
      sz += queue.debug_size();
    return sz;
  }

  int debug_cancelled_in_queue() const
  {
    int sz = 0;
    for (auto&& queue : m_queues)
      sz += queue.debug_cancelled_in_queue();
    return sz;
  }
#endif
};

} // namespace statefultask
