/**
 * @file
 * @brief Generate a timer event. Declaration of class AITimer.
 *
 * Copyright (C) 2012, 2017  Carlo Wood.
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
 *   07/02/2012
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#pragma once

#include "AIStatefulTask.h"
#include <atomic>

// A timer task.
//
// Before calling run(), call setInterval() to pass needed parameters.
//
// When the task finishes it calls the callback, use parameter _1,
// (success) to check whether or not the task actually timed out or
// was cancelled. The boolean is true when it expired and false if the
// task was aborted.
//
// Objects of this type can be reused multiple times, see
// also the documentation of AIStatefulTask.
//
// Typical usage:
//
// AITimer* timer = new AITimer;
//
// timer->setInterval(5.5);     // 5.5 seconds time out interval.
// timer->run(...);             // Start timer and pass callback; see AIStatefulTask.
//
// The default behavior is to call the callback and then delete the AITimer object.
// One can call run() again from the callback function to get a repeating expiration.
// You can call run(...) with parameters too, but using run() without parameters will
// just reuse the old ones (call the same callback).
//
class AITimer : public AIStatefulTask
{
 protected:
  // The base class of this task.
  using direct_base_type = AIStatefulTask;

  // The different states of the stateful task.
  enum timer_state_type {
    AITimer_start = direct_base_type::max_state,
    AITimer_expired
  };
 public:
  static state_type constexpr max_state = AITimer_expired + 1;

 private:
  std::atomic_bool mHasExpired;  	//!< Set to true after the timer expired.
  //AIFrameTimer mFrameTimer;           //!< The actual timer that this object wraps.
  double mInterval;                   //!< Input variable: interval after which the event will be generated, in seconds.

 public:
  AITimer(DEBUG_ONLY(bool debug = false)) :
#ifdef CWDEBUG
    AIStatefulTask(debug),
#endif
    mHasExpired(false), mInterval(0) { DoutEntering(dc::statefultask(mSMDebug), "AITimer() [" << (void*)this << "]"); }

  /**
   * @brief Set the interval after which the timer should expire.
   *
   * @param interval Amount of time in seconds before the timer will expire.
   *
   * Call abort() at any time to stop the timer (and delete the AITimer object).
   */
  void setInterval(double interval) { mInterval = interval; }

  /**
   * @brief Get the expiration interval.
   *
   * @returns expiration interval in seconds.
   */
  double getInterval() const { return mInterval; }

 protected:
  // Call finish() (or abort()), not delete.
  ~AITimer() override { DoutEntering(dc::statefultask(mSMDebug), "~AITimer() [" << (void*)this << "]"); /* mFrameTimer.cancel(); */ }

  // Implemenation of state_str for run states.
  char const* state_str_impl(state_type run_state) const override;

  // Handle initializing the object.
  void initialize_impl() override;

  // Handle mRunState.
  void multiplex_impl(state_type run_state) override;

  // Handle aborting from current bs_run state.
  void abort_impl() override;

 private:
  // This is the callback for mFrameTimer.
  void expired();
};
