/**
 * @file
 * @brief Declaration of class AIEngine.
 *
 * @Copyright (C) 2010 - 2013, 2017  Carlo Wood.
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
 *   01/03/2010
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   28/02/2013
 *   - Rewritten from scratch to fully support threading.
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#pragma once

#include "threadsafe/aithreadsafe.h"
#include "threadsafe/ConditionVariable.h"
#include "AIStatefulTask.h"
#include "debug.h"
#include <list>
#include <chrono>
#include <boost/intrusive_ptr.hpp>

/*!
 * @brief Task queue and dispatcher.
 *
 * This object dispatches tasks from \ref mainloop().
 *
 * Each of the member functions @link group_run AIStatefulTask::run()@endlink end with a call to <code>AIStatefulTask::reset()</code>
 * which in turn calls <code>AIStatefulTask::multiplex(initial_run)</code>.
 * When a default engine was passed to \c{run} then \c{multiplex} adds the task to the queue of that engine.
 * When a thread pool queue was passed to \c run then the task is added to that queue of the thread pool.
 * If the special \ref AIQueueHandle @link AIStatefulTask::Handler::immediate immediate@endlink was passed to \c run then the task is being run immediately in the
 * thread that called \c run and will <em>keep</em> running until it is either aborted or one of
 * @link AIStatefulTask::finish finish()@endlink, @link group_yield yield*()@endlink or @link group_wait wait*()@endlink
 * is called!
 *
 * Moreover, every time a task run with `immediate` as handler (and that didn't set a target handler) calls \c wait,
 * then the task will continue running immediately when some thread calls @link AIStatefulTask::signal signal()@endlink,
 * and again <em>keeps</em> running!
 *
 * If you don't want a call to \c run and/or \c signal to take too long, or it would not be thread-safe to not run the task from
 * the main loop of a thread, then either pass a default engine, a thread pool queue, or (when the default handler is Handler::immediate)
 * you've to make sure the task \htmlonly&dash;\endhtmlonly when (re)started \htmlonly&dash;\endhtmlonly quickly calls
 * <code>yield*()</code> or <code>wait*()</code> (again), causing the task to be added to the highest priority queue of the thread pool.
 *
 * Note that if during such engineless and queueless state @link AIStatefulTask::yield yield()@endlink is called <em>without</em>
 * passing a handler, then the task will be added to the highest priority queue of the thread pool.
 *
 * Sinds normally \htmlonly&dash;\endhtmlonly for some instance of AIEngine \htmlonly&dash;\endhtmlonly
 * it is the <em>same</em> thread that calls the AIEngine::mainloop member function in the main loop of that thread,
 * there is a one-on-one relationship between a thread and an AIEngine object.
 *
 * Once a task is added to an engine then every time the thread of that engine returns to its main loop,
 * it processes one or more tasks in its queue until either &mdash; all tasks are finished, idle, moved to another handler
 * or aborted &mdash; or, if a maximum duration was set, until more than @link AIEngine::AIEngine(char const*, float) max_duration@endlink
 * milliseconds was spent in the \c mainloop (this applies to new tasks, not a task whose \c multiplex_impl is already called
 * &mdash;only a frequent call to @link AIStatefulTask::yield yield()@endlink is your friend there).
 *
 * Note that each @link AIStatefulTask task@endlink object keeps track of three handlers:
 * * <code>AIStatefulTask::mTargetHandler&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;// Last handler passed to target() or yield*().</code>
 * * <code>AIStatefulTask::mState.current_handler&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;// While idle <code>Handler::idle</code>, otherwise the first non-idle handler from the top (of this list of three), or Handler::immediate</code>.
 * * <code>AIStatefulTask::mDefaultHandler&nbsp;// The handler passed to run() (that is 'immediate' when none was passed).</code>
 *
 * The first, \c mTargetHandler, is the handler that was passed to the last call of member
 * function AIStatefulTask::target (which is also called by the
 * @link group_yield AIStatefulTask::yield*()@endlink member functions that take an engine or handler as parameter).
 * It will be \c idle when \c target wasn't called yet, or when <code>Handler::idle</code> is
 * explicitly passed as handler to one of these member functions.
 *
 * The second, \c current_handler, is the handler that the task is added to \htmlonly&dash;\endhtmlonly for as long
 * as the task needs to be run. It is Handler::idle when task didn't run at all yet or doesn't need to run anymore (e.g., when it is idle).
 * As soon as this value is changed to a different value than the handler that the task
 * is currently active in then that handler will not run that task anymore and remove it from
 * its queue (if any); it is therefore the canonical handler that the task runs in.
 * If a task goes idle, this value is set to Handler::idle; otherwise it is set to the
 * last handler that that task did run in, which is the first non-idle handler from the top for the short list above.
 *
 * The last, \c mDefaultHandler, is the handler that is passed to @link group_run run@endlink and never changes.
 */
class AIEngine
{
 private:
  struct QueueElementComp;
  class QueueElement {
    private:
      boost::intrusive_ptr<AIStatefulTask> mStatefulTask;

    public:
      QueueElement(AIStatefulTask* stateful_task) : mStatefulTask(stateful_task) { }
      friend bool operator==(QueueElement const& e1, QueueElement const& e2) { return e1.mStatefulTask == e2.mStatefulTask; }
      friend bool operator!=(QueueElement const& e1, QueueElement const& e2) { return e1.mStatefulTask != e2.mStatefulTask; }
      friend struct QueueElementComp;

      AIStatefulTask const& stateful_task() const { return *mStatefulTask; }
      AIStatefulTask& stateful_task() { return *mStatefulTask; }
  };
  struct QueueElementComp {
    inline bool operator()(QueueElement const& e1, QueueElement const& e2) const;
  };

  using queued_type = std::list<QueueElement>;

  struct engine_state_st
  {
    queued_type list;
    bool waiting;
    engine_state_st() : waiting(false) { }
  };

  using engine_state_type = aithreadsafe::Wrapper<engine_state_st, aithreadsafe::policy::Primitive<aithreadsafe::ConditionVariable>>;

#ifndef DOXYGEN
 public:       // Used by AIStatefulTask.
  using clock_type = AIStatefulTask::clock_type;
  using duration_type = AIStatefulTask::duration_type;
#endif

 private:
  engine_state_type mEngineState;
  char const* mName;
  duration_type mMaxDuration;
  bool mHasMaxDuration;

 public:
  /*!
   * @brief Construct an AIEngine.
   *
   * The argument \a name must be a string-literal (only the pointer to it is stored).
   * If \a max_duration is less than or equal zero (the default) then no duration is set
   * and the engine won't return from \ref mainloop until all tasks in its queue either
   * finished, are waiting (idle) or did yield to a different engine.
   *
   * @param name A human readable name for this engine. Mainly used for debug output.
   * @param max_duration The maximum duration per loop (in milliseconds) during which new tasks are (re)started. See setMaxDuration.
   */
  AIEngine(char const* name, float max_duration = 0.0f) : mName(name) { setMaxDuration(max_duration); }

  /*!
   * @brief Add \a stateful_task to this engine.
   *
   * The task will remain assigned to the engine until it no longer @link AIStatefulTask::active active@endlink
   * (tested after returning from @link Example::multiplex_impl multiplex_impl@endlink).
   *
   * Normally you should not call this function directly. Instead, use @link group_run AIStatefulTask::run@endlink.
   *
   * @param stateful_task The task to add.
   */
  void add(AIStatefulTask* stateful_task);

  /*!
   * @brief The main loop of the engine.
   *
   * Run all tasks that were @link add added@endlink to the engine until
   * they are all finished and/or idle, or until mMaxDuration milliseconds
   * have passed if a maximum duration was set.
   */
  void mainloop();

  /*!
   * @brief Wake up a sleeping engine.
   */
  void wake_up();

  /*!
   * @brief Flush all tasks from this engine.
   *
   * All queued tasks are removed from the engine and marked as killed.
   * This can be used when terminating a program, just prior to destructing
   * all remaining objects, to avoid that tasks do call backs and use objects
   * that are being destructed.
   */
  void flush();

  /*!
   * @brief Return a human readable name of this engine.
   *
   * This is simply the string that was passed upon construction.
   */
  char const* name() const { return mName; }

  /*!
   * @brief Set mMaxDuration in milliseconds.
   *
   * The maximum time the engine will spend in \ref mainloop calling \c multiplex on unfinished and non-idle tasks.
   * Note that if the last call to \c multiplex takes considerable time then it is possible that the time spend
   * in \c mainloop will go arbitrarily far beyond \c mMaxDuration. It is the responsibility of the user to not
   * run states (of task) that can take too long in engines that have an \c mMaxDuration set.
   */
  void setMaxDuration(float max_duration);

  /*!
   * @brief Return true if a maximum duration was set.
   *
   * Note, only engines with a set maximum duration can be used to sleep
   * on by using AIStatefulTask::yield_frame or AIStatefulTask::yield_ms.
   */
  bool hasMaxDuration() const { return mHasMaxDuration; }
};
