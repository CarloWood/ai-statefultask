/**
 * ai-statefultask -- Asynchronous, Stateful Task Scheduler library.
 *
 * @file
 * @brief Implementation of AIStatefulTask.
 *
 * @Copyright (C) 2010 - 2013, 2017  Carlo Wood.
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

#include "sys.h"
#include "AIEngine.h"
#include "threadpool/AIThreadPool.h"

//==================================================================
// Overview

// A AIStatefulTask is a base class that allows derived classes to
// go through asynchronous states, while the code still appears to
// be more or less sequential.
//
// These task objects can be reused to build more complex objects.
//
// It is important to note that each state has a duality: the object
// can have a state that will cause a corresponding function to be
// called; and often that function will end with changing the state
// again, to signal that it was handled. It is easy to confuse the
// function of a state with the state at the end of the function.
// For example, the state "initialize" could cause the member
// function 'init()' to be called, and at the end one would be
// inclined to set the state to "initialized". However, this is the
// wrong approach: the correct use of state names does reflect the
// functions that will be called, never the function that just was
// called.
//
// Each (derived) class goes through a series of states as follows:
//
//   Creation
//       |
//       v
//     (idle) <----.    Idle until run() is called.
//       |         |
//   Initialize    |    Calls initialize_impl().
//       |         |
//       | (idle)  |    Idle until signal() is called.
//       |  |  ^   |
//       v  v  |   |
//   .-----------. |
//   | Multiplex | |    Call multiplex_impl() until wait(), abort() or finish() is called.
//   '-----------' |
//    |    |       |
//    v    |       |
//  Abort  |       |    Calls abort_impl().
//    |    |       |
//    v    v       |
//    Finish       |    Calls finish_impl(), which may call run().
//    |    |       |
//    |    v       |
//    | Callback   |    which may call kill() and/or run().
//    |  |   |     |
//    |  |   `-----'
//    v  v
//  Killed              Delete the task (all stateful tasks must be allocated with new).
//
// Each state causes corresponding code to be called.
// Finish cleans up whatever is done by Initialize.
// Abort should clean up additional things done while running.
//
// The running state is entered by calling run().
//
// While the base class is in the bs_multiplex state, it is the derived class
// that goes through different states. The state variable of the derived
// class is only valid while the base class is in the bs_multiplex state.
//
// A derived class can exit the bs_multiplex state by calling one of two methods:
// abort() in case of failure, or finish() in case of success.
// Respectively these set the state to bs_abort and bs_finish.
//
// The methods of the derived class call set_state() to change their
// own state within the bs_multiplex state, or by calling either abort()
// or finish().
//
// Restarting a finished stateful task can be done by calling run(),
// which will cause a re-initialize. The default is to destruct the
// task once the last boost::intrusive_ptr to it is deleted.
//


//==================================================================
// Declaration

#ifdef DOXYGEN  // Only defined while generating documentation.

/**
 * @anchor example_task
 * An example task class.
 *
 * Every stateful task is (indirectly) derived from AIStatefulTask.
 *
 * For example:
 * @code
 * class ExampleBase : public AIStatefulTask
 * {
 *   ...
 * };
 *
 * class Example : public ExampleBase
 * {
 *  protected:
 *   // Each derived class must declare direct_base_type to be the class that it is directly derived from.
 *   using direct_base_type = ExampleBase;
 *
 *   // Each derived class must define the states that it might go through.
 *   enum example_state_type {
 *     Example_start = direct_base_type::state_end,     // The first state must be equal to direct_base_type::state_end.
 *     Example_next,
 *     Example_foo,
 *     Example_done                                     // The last state is used to give state_end its value.
 *   };
 *
 *  public:
 *   // Each derived class must define a state_end that is one beyond its last state.
 *   static state_type constexpr state_end = Example_done + 1;
 *
 *  public:
 *   // The derived class must have a default constructor.
 *   Example();
 *
 *  protected:
 *  // The destructor of the derived class must be protected.
 *   ~Example() override;
 *
 *  protected:
 *    // The following two virtual functions must be implemented:
 *
 *    // Convert a state_type to a human readable string, for debug purposes.
 *    char const* state_str_impl(state_type run_state) const override;
 *
 *    // Actually run the task.
 *    void multiplex_impl(state_type run_state) override;
 *
 *    // The following virtual functions may be overridden (they have a default):
 *
 *    // Handle initializing the task object.
 *    // The default initialize_impl sets the starting state to AIStatefulTask::state_end,
 *    // which should be the first state of the derived class.
 *    void initialize_impl() override;
 *
 *    // Handle aborting from current bs_multiplex state.
 *    // The default abort_impl does nothing.
 *    void abort_impl() override;
 *
 *    // Handle cleaning up from initialization (or post abort) state.
 *    // The default finish_impl() does nothing.
 *    void finish_impl() override;
 * };
 * @endcode
 */
class Example : public AIStatefulTask
{
 protected:
  /**
   * Stringify a run state, for debugging output.
   *
   * @param run_state A user defined state.
   * @returns A string literal with the human readable name of the state.
   *
   * This is a virtual function of the base class AIStatefulTask and
   * must be overridden by every derived class.
   *
   * Example implementation:
   *
   * @code
   * char const* Example::state_str_impl(state_type run_state) const
   * {
   *   // If this fails then a derived class forgot to add an AI_CASE_RETURN for this state.
   *   ASSERT(run_state < state_end);
   *   switch(run_state)
   *   {
   *     // A complete listing of hello_world_state_type.
   *     AI_CASE_RETURN(Example_start);
   *     // ...
   *     AI_CASE_RETURN(Example_done);
   *   }
   *   return direct_base_type::state_str_impl(run_state);
   * }
   * @endcode
   */
  char const* state_str_impl(state_type run_state) const override;

  /**
   * Initialization of a task.
   *
   * The default @c initialize_impl sets the state to the first state,
   * as is done in the example below. When the default is overridden
   * then the new implementation must at least call @ref set_state once.
   *
   * Example implementation:
   *
   * @code
   * void Example::initialize_impl()
   * {
   *   set_state(Example_start);
   * }
   * @endcode
   */
  void initialize_impl() override;

  /**
   * @anchor multiplex_impl
   * The main implementation of the task. Run the task.
   *
   * @param run_state The current user defined state that the task is in.
   *
   * Example implementation:
   *
   * @code
   * void Example::multiplex_impl(state_type run_state)
   * {
   *   switch(run_state)
   *   {
   *     case Example_start:
   *       // Handle state Example_start here.
   *       break;
   *     case Example_next:
   *       // Handle state Example_next here.
   *       break;
   *     // ... etc.
   *     case Example_done:
   *       finish();
   *       break;
   *   }
   * }
   * @endcode
   */
  void multiplex_impl(state_type run_state) override;

  /**
   * Handle aborting the task.
   *
   * Example implementation:
   *
   * @code
   * void Example::abort_impl()
   * {
   * }
   * @endcode
   */
  void abort_impl() override;

  /**
   * Handle finishing the task.
   *
   * Example implementation:
   *
   * @code
   * void Example::finish_impl()
   * {
   * }
   * @endcode
   */
  void finish_impl() override;

  /**
   * Handle being force killed.
   *
   * This member function is called when a task is running in an AIEngine
   * and that engine is flushed (by calling AIEngine::flush()). The result
   * is that the task just stops running cold. Neither abort_impl() nor
   * finish_impl() is called: it just stops getting any CPU cycles and
   * should be destructed shortly.
   *
   * You probably will never to override this function. In fact, it
   * will never be called unless you call AIEngine::flush() yourself
   * and its main purpose is to avoid an assert in the destructor of
   * the tasks (because otherwise their internal state would show
   * they are still running which is an error otherwise).
   *
   * Example implementation:
   *
   * @code
   * void Example::force_killed()
   * {
   *   direct_base_class::force_killed();
   *   // Handle being forcefully killed.
   * }
   * @endcode
   */
  void force_killed() override;
};
#endif // EXAMPLE_CODE

//==================================================================
// Life cycle: creation, initialization, running and destruction

// Any thread may create a stateful task object, initialize it by calling
// it's initializing member function and call one of the 'run' methods,
// which might or might not immediately start to execute the task.

#ifdef EXAMPLE_CODE
Example* hello_world = new Example;
hello_world->init(...);         // A custom initialization function.
hello_world->run(...);          // One of the run() functions.
// hello_world might be destructed here.
// You can avoid possible destruction by using an boost::intrusive_ptr<Example>
// instead of Example*.
#endif // EXAMPLE_CODE

// The call to run() causes a call to initialize_impl(), which MUST call
//   set_state() at least once (only the last call is used).
// Upon return from initialize_impl(), multiplex_impl() will be called
//   with that state.
// multiplex_impl() may never reentrant (cause itself to be called).
// multiplex_impl() should end by callling either one of:
//   wait(), yield*(), finish() [or abort()].
// Leaving multiplex_impl() without calling any of those might result in an
//   immediate reentry, which could lead to 100% CPU usage unless the state
//   is changed with set_state().
// If multiplex_impl() calls finish() then finish_impl() will be called [if it
//   calls abort() then abort_impl() will called, followed by finish_impl()].
// Upon return from multiplex_impl(), and if finish() [or abort()] was called,
//   the call back passed to run() will be called.
// Upon return from the call back, the task object might be destructed
//   (see below).
// If wait(condition) was called, and there was only one call to signal(condition)
//   since the last call to wait(condition), then multiplex_impl() will not be
//   called again until signal(condition) is called from outside.
//
// If the call back function does not call run(), then the task is
//   deleted when the last boost::intrusive_ptr<> reference is deleted.
// If kill() is called after run() was called, then the call to run() is ignored.


//==================================================================
// Aborting

// If abort() is called before initialize_impl() is entered, then the
//   task is destructed after the last boost::intrusice_ptr<> reference
//   to it is deleted (if any). Note that this is only possible when a
//   child task is aborted before the parent even runs.
//
// if wait(), abort() or finish() are called inside its multiplex_impl()
//   then that multiplex_impl() should return immediately after.
//


//==================================================================
// Thread safety

// Only one thread can "run" a stateful task at a time; can call 'multiplex_impl'.
//
// Only from inside multiplex_impl (set_state also from initialize_impl), any of the
// following functions can be called:
//
// - set_state(new_state)       --> Force the state to new_state. This voids any previous call to set_state().
// - wait(condition)            --> Go idle (do nothing until signal(condition) is called), however if signal(condition)
//                                  was already called then multiplex_impl will be reentered immediately.
// - finish()                   --> Disables any scheduled runs.
//                              --> finish_impl --> [optional] kill()
//                              --> call back
//                              --> [optional] delete
//                              --> [optional] reset, upon return from multiplex_impl, call initialize_impl and start again at the top of multiplex.
// - yield([engine])            --> give CPU to other tasks before running again, run next from a stateful task engine.
//                                  If no engine is passed, the task will run in it's default engine (as set during construction).
// - yield_frame(engine, frames)/yield_ms(engine, ms)   --> yield(engine)
//
// the following function may be called from multiplex_impl() of any task (and thus by any thread):
//
// - abort()                    --> abort_impl
//                              --> finish()
//
// while the following functions may be called from anywhere (and any thread):
//
// - signal(condition)          --> if 'condition' matches the condition passed to the last call to wait() then schedule a run.
//
// In the above "scheduling a run" means calling multiplex_impl(), but the same holds for any *_impl()
// and the call back: Whenever one of those have to be called, thread_safe_impl() is called to
// determine if the current task allows that function to be called by the current thread,
// and if not - by which thread it should be called then (either main thread, or a special task
// thread). If thread switching is necessary, the call is literally scheduled in a queue of one
// of those two, otherwise it is run immediately.
//
// However, since only one thread at a time may be calling any *_impl function (except thread_safe_impl())
// or the call back function, it is possible that at the moment scheduling is necessary another thread
// is already running one of those functions. In that case thread_safe_impl() does not consider the
// current thread, but rather the running thread and does not do any scheduling if the running thread
// is ok, rather marks the need to continue running which should be picked up upon return from
// whatever the running thread is calling.

#ifdef CWDEBUG
char const* AIStatefulTask::event_str(event_type event)
{
  switch (event)
  {
    AI_CASE_RETURN(initial_run);
    AI_CASE_RETURN(schedule_run);
    AI_CASE_RETURN(normal_run);
    AI_CASE_RETURN(insert_abort);
  }
  ASSERT(false);
  return "UNKNOWN EVENT";
}
#endif

bool AIStatefulTask::waiting() const
{
  multiplex_state_type::crat state_r(mState);
  return state_r->base_state == bs_multiplex && sub_state_type::crat(mSubState)->idle;
}

bool AIStatefulTask::waiting_or_aborting() const
{
  multiplex_state_type::crat state_r(mState);
  return state_r->base_state == bs_abort || (state_r->base_state == bs_multiplex && sub_state_type::crat(mSubState)->idle);
}

void AIStatefulTask::multiplex(event_type event, Handler handler)
{
  // If this fails then you are using a pointer to a stateful task instead of an boost::intrusive_ptr<AIStatefulTask>.
//  ASSERT(event == initial_run || ref_used());

  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::multiplex(" << event_str(event) << ", " << handler << ") [" << (void*)this << "]");

  // Paranoia; this can be removed after a while. As a result of this, handler is true when event == normal_run.
  ASSERT(event != normal_run || handler);

  base_state_type state;
  state_type run_state;
  bool waiting;

  // Critical area of mState.
  {
    multiplex_state_type::rat state_r(mState);

    // This would be an almost impossible race condition.
    if (AI_UNLIKELY(event == insert_abort && state_r->base_state != bs_multiplex))
    {
      Dout(dc::statefultask(mSMDebug), "Leaving because the task finished in the meantime [" << (void*)this << "]");
      return;
    }

    if (!(event != normal_run || handler == state_r->current_handler))
    {
      Dout(dc::statefultask(mSMDebug), "Leaving because current_handler isn't equal to calling handler [" << (void*)this << "]");
      return;
    }
    // We get here and event == normal_run then handler == state_r->current_handler
    // and as a result of the second ASSERT that means that state_r->current_handler
    // isn't 'idle'.

    // multiplex(schedule_run) is only called from signal(condition) provided that
    // mSubState.idle & condition is non-zero, which is never true when idle is set to
    // zero; idle is set to zero upon a call to abort() or finish() which are
    // the only two ways to leave the bs_multiplex state. And since idle is only
    // set by a call to wait(), which may only be called from multiplex_impl, we can
    // be sure to be in the bs_multiplex state when multiplex(schedule_run) is called.
    //
    // multiplex(insert_abort) is only called while the task is still running (see
    // abort()), that is, very shortly after releasing the lock on mState during which
    // this was tested. In the extremely unlikely case that this changed in the meantime
    // we already left this function in the above test.
    //
    // multiplex(normal_run) is only called from AIEngine::mainloop() for tasks
    // in the engines queue (which are removed when current_engine stops being
    // equal to that engine; and we return if the above test fails anyway). Hence,
    // we get here only for tasks with a non-null current_engine, but for any base state.
    ASSERT(event != initial_run || state_r->base_state == bs_reset);
    ASSERT((event != schedule_run && event != insert_abort) || state_r->base_state == bs_multiplex);

    // If another thread is already running multiplex() then it will pick up
    // our need to run (by us having set need_run), so there is no need to run
    // ourselves.
    ASSERT(!mMultiplexMutex.is_self_locked());          // We may never enter recursively!
    if (!mMultiplexMutex.try_lock())
    {
      // This just should never happen; a call to run() should always set the base state beyond bs_reset.
      ASSERT(event != initial_run);
      // FIXME: when does this happen?
      // If a task can be (attempted to be) run in parallel, then isn't there a race condition
      // in threadpool where the 'active' state of the task is determined by calling active(handler)
      // immediately after returning from multiplex()?
      ASSERT(false);
      Dout(dc::statefultask(mSMDebug), "Leaving because it is already being run [" << (void*)this << "]");
      return;
    }

    //===========================================
    // Start of critical area of mMultiplexMutex.

    // If another thread already called begin_loop() since we needed a run,
    // then we must not schedule a run because that could lead to running
    // the same state twice. Note that if need_run was reset in the mean
    // time and then set again, then it can't hurt to schedule a run since
    // we should indeed run, again.
    if (event == schedule_run && !sub_state_type::rat(mSubState)->need_run)
    {
      mMultiplexMutex.unlock();
      Dout(dc::statefultask(mSMDebug), "Leaving because it was already being run [" << (void*)this << "]");
      return;
    }

    // We're at the beginning of multiplex, about to actually run it.
    // Make a copy of the states.
    waiting = state_r->wait_condition != nullptr;
    state = state_r->base_state;
    run_state = begin_loop();
  }
  // End of critical area of mState.

  bool keep_looping;
  bool destruct = false;
  do
  {
#ifdef CWDEBUG
    debug::Mark __mark;
#endif

    if (event == normal_run)
    {
#ifdef CWDEBUG
      if (state == bs_multiplex)
      {
        if (waiting)
          Dout(dc::statefultask(mSMDebug), "Testing wait condition... [" << (void*)this << "]");
        else
          Dout(dc::statefultask(mSMDebug), "Running state bs_multiplex / " << state_str_impl(run_state) << " [" << (void*)this << "]");
      }
      else
        Dout(dc::statefultask(mSMDebug), "Running state " << state_str(state) << " [" << (void*)this << "]");
#endif

#if CW_DEBUG
      // This debug code checks that each task steps precisely through each of it's states correctly.
      if (state != bs_reset)
      {
        switch (mDebugLastState)
        {
          case bs_reset:
            ASSERT(state == bs_initialize || state == bs_killed);
            break;
          case bs_initialize:
            ASSERT(state == bs_multiplex || state == bs_abort);
            break;
          case bs_multiplex:
            ASSERT(state == bs_multiplex || state == bs_finish || state == bs_abort);
            break;
          case bs_abort:
            ASSERT(state == bs_finish);
            break;
          case bs_finish:
            ASSERT(state == bs_callback);
            break;
          case bs_callback:
            ASSERT(state == bs_killed || state == bs_reset);
            break;
          case bs_killed:
            ASSERT(state == bs_killed);
            break;
        }
      }
      // More sanity checks.
      if (state == bs_multiplex)
      {
        // set_state is only called from multiplex_impl and therefore synced with mMultiplexMutex.
        mDebugShouldRun |= mDebugSetStatePending;
        // Should we run at all?
        ASSERT(mDebugShouldRun);
      }
      // Any previous reason to run is voided by actually running.
      mDebugShouldRun = false;
#endif

      mRunMutex.lock();
      // Now we are actually running a single state.
      // If abort() was called at any moment before, we execute that state instead.
      bool const late_abort = (state == bs_multiplex || state == bs_initialize) && sub_state_type::rat(mSubState)->aborted;
      if (AI_UNLIKELY(late_abort))
      {
        // abort() was called from a child task, from another thread, while we were already scheduled to run normally from an engine.
        // What we want to do here is pretend we detected the abort at the end of the *previous* run.
        // If the state is bs_multiplex then the previous state was either bs_initialize or bs_multiplex,
        // both of which would have switched to bs_abort: we set the state to bs_abort instead and just
        // continue this run.
        // However, if the state is bs_initialize we can't switch to bs_killed because that state isn't
        // handled in the switch below; it's only handled when exiting multiplex() directly after it is set.
        // Therefore, in that case we have to set the state BACK to bs_reset and run it again. This duplicated
        // run of bs_reset is not a problem because it happens to be a NoOp.
        state = (state == bs_initialize) ? bs_reset : bs_abort;
#ifdef CWDEBUG
        Dout(dc::statefultask(mSMDebug), "Late abort detected! Running state " << state_str(state) << " instead [" << (void*)this << "]");
#endif
      }
#if CW_DEBUG
      mDebugLastState = state;
      // Make sure we only call ref() once and in balance with unref().
      if (state == bs_initialize)
      {
        // This -- and call to ref() (and the test when we're about to call unref()) -- is all done in the critical area of mMultiplexMutex.
        ASSERT(!mDebugRefCalled);
        mDebugRefCalled = true;
      }
#endif
      switch (state)
      {
        case bs_reset:
          // We're just being kick started to get into the right thread
          // (possibly for the second time when a late abort was detected, but that's ok: we do nothing here).
          break;
        case bs_initialize:
          inhibit_deletion(DEBUG_ONLY(false));  // false because it is actually OK here when the corresponding allow_deletion() immediately deletes this object.
          initialize_impl();
          break;
        case bs_multiplex:
          ASSERT(!mDebugAborted);
          if (!waiting)
          {
            AIStatefulTask* prev_task = tl_parent_task;
            tl_parent_task = this;
            multiplex_impl(run_state);
            tl_parent_task = prev_task;
          }
          else
          {
            multiplex_state_type::wat state_w(mState);
            if (!state_w->wait_condition())
              wait(state_w->conditions);
            else
            {
              state_w->wait_condition = nullptr;
              sub_state_type::wat(mSubState)->idle = 0;
#if CW_DEBUG
              mDebugShouldRun = true;
#endif
            }
          }
          break;
        case bs_abort:
          abort_impl();
          break;
        case bs_finish:
          sub_state_type::wat(mSubState)->reset = false;        // By default, halt tasks when finished.
          finish_impl();                                        // Call run() from finish_impl() or the call back to restart from the beginning.
          break;
        case bs_callback:
          callback();
          break;
        case bs_killed:
          mRunMutex.unlock();
          // bs_killed is handled when it is set. So, this must be a re-entry.
          // We can only get here when being called by an engine that we were added to before we were killed.
          // This should already be have been set to idle to indicate that we want to be removed from that engine.
          ASSERT(!multiplex_state_type::rat(mState)->current_handler);
          // Do not call unref() twice.
          return;
      }
      mRunMutex.unlock();
    }

    {
      multiplex_state_type::wat state_w(mState);

      //=================================
      // Start of critical area of mState

      // Unless the state is bs_multiplex or bs_killed, the task needs to keep calling multiplex().
      bool need_new_run = true;
      if (event == normal_run || event == insert_abort)
      {
        sub_state_type::rat sub_state_r(mSubState);

        if (event == normal_run)
        {
          // Switch base state as function of sub state.
          switch (state)
          {
            case bs_reset:
              if (sub_state_r->aborted)
              {
                // We have been aborted before we could even initialize, no de-initialization is possible.
                state_w->base_state = bs_killed;
                // Stop running.
                need_new_run = false;
              }
              else
              {
                // run() was called: call initialize_impl() next.
                state_w->base_state = bs_initialize;
              }
              break;
            case bs_initialize:
              if (sub_state_r->aborted)
              {
                // initialize_impl() called abort.
                state_w->base_state = bs_abort;
              }
              else
              {
                // Start actually running.
                state_w->base_state = bs_multiplex;
                // If the state is bs_multiplex we only need to run again when need_run was set again in the meantime or when this task isn't idle.
                need_new_run = sub_state_r->need_run || !sub_state_r->idle;
              }
              break;
            case bs_multiplex:
              if (sub_state_r->aborted)
              {
                // abort() was called.
                state_w->base_state = bs_abort;
              }
              else if (sub_state_r->finished)
              {
                // finish() was called.
                state_w->base_state = bs_finish;
              }
              else
              {
                // Continue in bs_multiplex.
                // If the state is bs_multiplex we only need to run again when need_run was set again in the meantime or when this task isn't idle.
                need_new_run = sub_state_r->need_run || !sub_state_r->idle;
                // If this fails then the run state didn't change and neither wait() nor yield() was called.
                ASSERT(!(need_new_run && !mYield && sub_state_r->run_state == run_state &&
                       !(sub_state_r->aborted ||        // abort was called.
                         sub_state_r->finished ||       // finish was called.
                         sub_state_r->wait_called ||    // wait was called.
                         waiting)));                    // wait_condition just became true.
              }
              break;
            case bs_abort:
              // After calling abort_impl(), call finish_impl().
              state_w->base_state = bs_finish;
              break;
            case bs_finish:
              // After finish_impl(), call the call back function.
              state_w->base_state = bs_callback;
              break;
            case bs_callback:
              if (sub_state_r->reset)
              {
                // run() was called (not followed by kill()).
                state_w->base_state = bs_reset;
              }
              else
              {
                // After the call back, we're done.
                state_w->base_state = bs_killed;
                // Call unref().
                destruct = true;
                // Stop running.
                need_new_run = false;
              }
              break;
            default: // bs_killed
              // We never get here.
              break;
          }
        }
        else // event == insert_abort
        {
          // We have been aborted, but we're idle. If we'd just schedule a new run below, it would re-run
          // the last state before the abort is handled. What we really need is to pick up as if the abort
          // was handled directly after returning from the last run. If we're not running anymore, then
          // do nothing as the task already ran and things should be processed normally
          // (in that case this is just a normal schedule which can't harm because we can't accidently
          // re-run an old run_state).
          if (state_w->base_state == bs_multiplex)      // Still running?
          {
            // See the switch above for case bs_multiplex.
            ASSERT(sub_state_r->aborted);
            // abort() was called.
            state_w->base_state = bs_abort;
          }
        }

#ifdef CWDEBUG
        if (state != state_w->base_state)
          Dout(dc::statefultask(mSMDebug), "Base state changed from " << state_str(state) << " to " << state_str(state_w->base_state) <<
              "; need_new_run = " << (need_new_run ? "true" : "false") << " [" << (void*)this << "]");
#endif
      }

      // Figure out in which handler we should run, assuming we have to run at all.
      // Because mDefaultHandler is never 'idle', handler will never be 'idle'.
      Handler handler = mTargetHandler              ? mTargetHandler :
                        state_w->current_handler    ? state_w->current_handler :
                        mDefaultHandler;

      // And the current handler we're running in.
      Handler current_handler = (event == normal_run) ? state_w->current_handler : Handler(Handler::immediate);
      // Since state_w->current_handler can't be idle when event == normal_run, current_handler will never be idle either.
      ASSERT(current_handler);

      // Immediately run again if yield() wasn't called and it's OK to run in this thread.
      keep_looping = need_new_run && !mYield && handler == current_handler;
      mYield = false;

      // We can't satisfy running in an immediate handler and not keep_looping at the same time, provided we need to run at all of course.
      if (!keep_looping && need_new_run && handler.is_immediate())
      {
        // Note this means that mYield was true (aka, the user called yield()).
        //
        // Namely, if handler would be unequal current_handler, then current_handler is not immediate, therefore
        // we know that event == normal_run and current_handler == state_w->current_handler. In turn that means
        // that state_w->current_handler is also not idle. That is a contradiction because then handler is
        // either mTargetHandler or state_w->current_handler and neither is immediate (it is not allowed to
        // use immediate as target handler).
        //
        // Hence-- we can only get here when the user called yield() (without a target).
        // Since it is rather easy to get here with state_w->current_handler set to immediate,
        // the user should either set a (non immediate) default handler or never use yield().
        //
        // Either set a non-immediate default handler when calling run(), or don't use yield() without a handler argument.
        ASSERT(!mDefaultHandler.is_immediate());
        handler = mDefaultHandler;
      }

      Dout(dc::statefultask(mSMDebug && !keep_looping),
          (!need_new_run ? state_w->current_handler ? state_w->current_handler.is_engine() ? "No need to run, removing from engine"
                                                                                           : "No need to run, removing from thread pool"
                                                    : "No need to run"
                         : handler.is_engine() ? "Need to run, adding to engine"
                                               : "Need to run, adding to thread pool"
          ) << " [" << (void*)this << "]");

      if (keep_looping)
      {
        // Start a new loop.
        waiting = state_w->wait_condition != nullptr;
        state = state_w->base_state;
        run_state = begin_loop();
        if (event != normal_run)
        {
          // I don't think current_handler is set for not-normal_run's. If this fails then it might be needed to leave current_handler alone.
          ASSERT(!state_w->current_handler);
          event = normal_run;
          state_w->current_handler = Handler::immediate;
        }
      }
      else
      {
        if (need_new_run)
        {
          // Add us to an engine / thread pool if necessary.
          if (handler != state_w->current_handler)
          {
            // Mark that we want to run in this engine (thread pool), and at the same time, that we don't want to run in the previous one.
            state_w->current_handler = handler;
            if (handler.is_engine())
            {
              // Actually add the task to the engine.
              handler.m_handle.engine->add(this);
            }
            else
            {
              ASSERT(handler.is_thread_pool());
              add_task_to_thread_pool(handler.get_queue_handle());
            }
          }
#if CW_DEBUG
          // We are leaving the loop, but we're not idle. The task should re-enter multiplex() again.
          mDebugShouldRun = true;
#endif
        }
        else
        {
          // Remove this task from any engine,
          // causing the engine to remove us.
          state_w->current_handler = Handler::idle;
        }

#if CW_DEBUG
        // Mark that we stop running the loop.
        mThreadId = std::thread::id();

        if (destruct)
        {
          // We're about to call unref(). Make sure we call that in balance with ref()!
          ASSERT(mDebugRefCalled);
          mDebugRefCalled  = false;
        }
#endif

        // End of critical area of mMultiplexMutex.
        //=========================================

        // Release the lock on mMultiplexMutex *first*, before releasing the lock on mState,
        // to avoid to ever call the try_lock() and fail, while this thread isn't still
        // BEFORE the critical area of mState!

        mMultiplexMutex.unlock();
      }

      // Now it is safe to leave the critical area of mState as the try_lock won't fail anymore.
      // (Or, if we didn't release mMultiplexMutex because keep_looping is true, then this
      // end of the critical area of mState is equivalent to the first critical area in this
      // function.

      // End of critical area of mState.
      //================================
    }
  }
  while (keep_looping);

  if (destruct)
  {
    allow_deletion();
  }
}

//static
thread_local AIStatefulTask* AIStatefulTask::tl_parent_task;

void AIStatefulTask::add_task_to_thread_pool(AIQueueHandle queue_handle, uint8_t const failure_count)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::add_task_to_thread_pool(" << queue_handle << ", " << (int)failure_count << ")");

  // Add the task to the thread pool.
  AIThreadPool& thread_pool{AIThreadPool::instance()};
  auto queues_access = thread_pool.queues_read_access();
  auto& queue = thread_pool.get_queue(queues_access, queue_handle);
  int const capacity = queue.capacity();
  int length;
  {
    auto access = queue.producer_access();
    length = access.length();
    if (length < capacity) // Buffer not full?
    {
      boost::intrusive_ptr<AIStatefulTask> task(this);
      access.move_in(
          [task]()
          {
            // FIXME: I am not sure if the following is correct? It seems not, because
            // this task is now being added to the thread pool. If before it is executed
            // the current_handler is set to an engine then due to the line below it
            // would run in that engine?!
            Handler const handler{multiplex_state_type::crat(task->mState)->current_handler};
            // While if in the meantime current_handler is set to idle, then we need to bail.
            if (!handler)
              return false;
            task->multiplex(normal_run, handler);
            return task->active(handler);
          }
      );
    }
  }
  if (AI_UNLIKELY(length == capacity))
  {
    Dout(dc::warning|continued_cf, "Threadpool queue " << queue_handle << " full, can not run [" << this << "].");
    // We should add something to the thread pool that executes task->multiplex(normal_run, handler);
    // but we can't because the threadpool queue is full. Pass it to the magical function `defer` instead.
    boost::intrusive_ptr<AIStatefulTask> task(this);
    // Stop the responsible parent task, if any.
    if (tl_parent_task)
    {
      Dout(dc::finish, " Slowing down parent task " << tl_parent_task << ".");
      boost::intrusive_ptr<AIStatefulTask> parent_task(tl_parent_task);
      // Since tl_parent_task is set prior to calling multiplex_impl on that task (and reset after returning)
      // we are now inside tl_parent_task->multiplex_impl and not yet idle (we're still running, clearly).
      // Hence it is OK to assert on wait() not having been called yet (with a condition from OR_conditions_mask)
      // as if that were the case multiplex_impl should immediately return.
      parent_task->wait_AND(slow_down_condition);
      // This will call the lamba after a while.
#ifdef CWDEBUG
      parent_task->m_may_not_be_deleted = true;
#endif
      thread_pool.defer(queue_handle, failure_count, [parent_task, task, queue_handle, failure_count]()
          {
            task->add_task_to_thread_pool(queue_handle, failure_count + 1);
            parent_task->signal(slow_down_condition);
#ifdef CWDEBUG
            parent_task->m_may_not_be_deleted = false;
#endif
          });
    }
    else
    {
      Dout(dc::finish, "");
      // This will call the lamba after a while.
      thread_pool.defer(queue_handle, failure_count, [task, failure_count, queue_handle]()
          {
            task->add_task_to_thread_pool(queue_handle, failure_count + 1);
          });
    }
  }
  else
    queue.notify_one();
}

AIStatefulTask::state_type AIStatefulTask::begin_loop()
{
  sub_state_type::wat sub_state_w(mSubState);
  // Mark that we're about to honor all previous run requests.
  sub_state_w->need_run = false;
#if CW_DEBUG
  // Mark that we're currently not idle and wait() wasn't called (yet).
  sub_state_w->wait_called = false;
#endif
  // Paranoia check: we shouldn't be running when we're idle?!
  ASSERT(!sub_state_w->idle);

#if CW_DEBUG
  // Mark that we're running the loop.
  mThreadId = std::this_thread::get_id();
  // This point marks handling wait() with pending signal().
  mDebugShouldRun |= mDebugSignalPending;
  mDebugSignalPending = false;
#endif

  // Make a copy of the state that we're about to run.
  return sub_state_w->run_state;
}

/// Write a Handler to an ostream.
std::ostream& operator<<(std::ostream& os, AIStatefulTask::Handler const& handler)
{
  switch (handler.m_type)
  {
    case AIStatefulTask::Handler::idle_h:
      os << "<idle>";
      break;
    case AIStatefulTask::Handler::immediate_h:
      os << "<immediate>";
      break;
    case AIStatefulTask::Handler::engine_h:
      os << '"' << handler.m_handle.engine->name() << '"';
      break;
    case AIStatefulTask::Handler::thread_pool_h:
      os << handler.m_handle.queue_handle;
      break;
  }
  return os;
}

void AIStatefulTask::run(Handler default_handler, AIStatefulTask* parent, condition_type condition, on_abort_st on_abort)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::run(" <<
      "default_handler = " << default_handler <<
      ", " << (void*)parent << ", condition = " << std::hex << condition << std::dec <<
      ", on_abort = " << ((on_abort == abort_parent) ? "abort_parent" : (on_abort == signal_parent) ? "signal_parent" : "do_nothing") <<
      ") [" << (void*)this << "]");

  // You can't request 'idle' as default handler.
  ASSERT(default_handler);

#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // Can only be run when in one of these states.
    ASSERT(state_r->base_state == bs_reset || state_r->base_state == bs_finish || state_r->base_state == bs_callback);
    // Must be the first time we're being run, or we must be called from finish_impl or a callback function.
    ASSERT(!(state_r->base_state == bs_reset && (mParent || mCallback)));
  }
#endif

  // Do not change the mDefaultHandler when we're run() from a callback.
  if (!(mParent || mCallback) || !default_handler.is_immediate())
  {
    // Store the requested default handler.
    mDefaultHandler = default_handler;
  }
  else
    Dout(dc::statefultask(mSMDebug), "Keeping " << mDefaultHandler << " as default handler.");

  // Initialize sleep timer.
  mSleep = 0;

  // Allow nullptr to be passed as parent to signal that we want to reuse the old one.
  if (parent)
  {
    mParent = parent;
    // In that case remove any old callback!
    if (mCallback)
      mCallback = nullptr;

    mParentCondition = condition;
    mOnAbort = on_abort;
  }

  // If abort_parent is requested then a parent must be provided.
  ASSERT(on_abort == do_nothing || mParent);
  // If a parent is provided, it must be running.
  ASSERT(!mParent || mParent->running());

  // Start from the beginning.
  reset();
}

void AIStatefulTask::run(Handler default_handler, std::function<void (bool)> cb_function)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::run(default_handler = " << default_handler << ", <callback>) [" << (void*)this << "]");

  // You can't request 'idle' as default handler.
  ASSERT(default_handler);

#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // Can only be run when in one of these states.
    ASSERT(state_r->base_state == bs_reset || state_r->base_state == bs_finish || state_r->base_state == bs_callback);
    // Must be the first time we're being run, or we must be called from finish_impl or a callback function.
    ASSERT(!(state_r->base_state == bs_reset && (mParent || mCallback)));
  }
#endif

  // Store the requested default handler.
  mDefaultHandler = default_handler;

  // Initialize sleep timer.
  mSleep = 0;

  // Clean up any old callbacks.
  mParent = nullptr;

  // Create new call back.
  mCallback = std::move(cb_function);

  // Start from the beginning.
  reset();
}

void AIStatefulTask::callback()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::callback() [" << (void*)this << "]");

  bool aborted = sub_state_type::rat(mSubState)->aborted;
  if (mParent)
  {
    // It is possible that the parent is not running when the parent is in fact aborting and called
    // abort on this object from it's abort_impl function. It that case we don't want to recursively
    // call abort again (or change it's state).
    if (mParent->running())
    {
      if (aborted && mOnAbort == abort_parent)
      {
        mParent->abort();
        mParent = nullptr;
      }
      else if (!aborted || mOnAbort == signal_parent)
      {
        mParent->signal(mParentCondition);
      }
    }
  }
  if (mCallback)
  {
    mCallback(!aborted);
    if (!sub_state_type::rat(mSubState)->reset) // run() wasn't called from the callback (or before from finish())?
    {
      mCallback = nullptr;
      mParent = nullptr;
    }
  }
  else
  {
    // Not restarted by callback. Allow run() to be called later on.
    mParent = nullptr;
  }
}

char const* AIStatefulTask::state_str_impl(state_type) const
{
  // If this fails then a derived class forgot to add an AI_CASE_RETURN for this state.
  ASSERT(false);
  return "UNKNOWN STATE";
}

void AIStatefulTask::initialize_impl()
{
  Dout(dc::statefultask, "Calling default initialize_impl() [" << (void*)this << "]");
  // Start with the first state of the derived class.
  set_state(state_end);
}

void AIStatefulTask::abort_impl()
{
  Dout(dc::statefultask(mSMDebug), "Calling default abort_impl() [" << (void*)this << "]");
}

void AIStatefulTask::finish_impl()
{
  Dout(dc::statefultask(mSMDebug), "Calling default finish_impl() [" << (void*)this << "]");
}

void AIStatefulTask::force_killed()
{
  multiplex_state_type::wat(mState)->base_state = bs_killed;
}

void AIStatefulTask::kill()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::kill() [" << (void*)this << "]");
#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // kill() may only be called from the call back function.
    ASSERT(state_r->base_state == bs_callback);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
#endif
  // Void last call to run() (ie from finish_impl()), if any.
  sub_state_type::wat(mSubState)->reset = false;
}

void AIStatefulTask::reset()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::reset() [" << (void*)this << "]");
#if CW_DEBUG
  mDebugAborted = false;
  mDebugSignalPending = false;
  mDebugSetStatePending = false;
  mDebugRefCalled = false;
#endif
  mDuration = AIEngine::duration_type::zero();
  bool inside_multiplex;
  {
    multiplex_state_type::rat state_r(mState);
    // reset() is only called from run(), which may only be called when just created, from finish_impl() or from the call back function.
    ASSERT(state_r->base_state == bs_reset || state_r->base_state == bs_finish || state_r->base_state == bs_callback);
    inside_multiplex = state_r->base_state != bs_reset;
  }
  {
    sub_state_type::wat sub_state_w(mSubState);
    // Reset.
    sub_state_w->aborted = sub_state_w->finished = false;
    // Signal that we want to start running from the beginning.
    sub_state_w->reset = true;
    // We're not waiting for a condition.
    sub_state_w->idle = 0;
    sub_state_w->skip_wait = 0;
    // Keep running till we reach at least bs_multiplex.
    sub_state_w->need_run = true;
  }
  if (!inside_multiplex)
  {
    // Kickstart the task.
    multiplex(initial_run);
  }
}

void AIStatefulTask::set_state(state_type new_state)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::set_state(" << state_str_impl(new_state) << ") [" << (void*)this << "]");
#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // set_state() may only be called from initialize_impl() or multiplex_impl().
    ASSERT(state_r->base_state == bs_initialize || state_r->base_state == bs_multiplex);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
#endif
  {
    sub_state_type::wat sub_state_w(mSubState);
    // It should never happen that set_state() is called while we're idle,
    // unless we just called wait(). It is ok/allowed to call set_state
    // after a call to wait() to set the state we want to continue after
    // receiving a signal().
    ASSERT(sub_state_w->wait_called || !sub_state_w->idle);
    // Force current state to the requested state.
    sub_state_w->run_state = new_state;
#if CW_DEBUG
    // We should run. This can only be canceled by a call to wait().
    mDebugSetStatePending = !sub_state_w->wait_called;
#endif
  }
}

// The signal/wait system is intended to allow a task to start
// something that will result in a signal (when finished) and
// then go to sleep until that signal happened.
//
// Typically this looks like this:
//
//   case Test_Obtain:
//     set_state(Test_Obtained);
//     start_obtaining(condition_obtained);
//     wait(condition_obtained);
//     break;
//   case Test_Obtained:
//
// For proper functioning a few rules have to be followed:
//
// 1) wait() may only be called from multiplex_impl.
// 2) Any call to wait() must immediately be followed by a break;
// 3) Each wait() should use a different condition bit.
//
// Since each wait() is immediately followed by a break;
// it is not possible to have two wait()'s after another
// without going first through begin_loop(), which is the
// function that returns the run_state passed to multiplex_impl.
//
// In the simplest case, a call to wait(bit) causes the task
// to go idle until signal(bit) is received. Any subsequent
// signals are irrelevant since we shouldn't be waiting for
// the same bit again.
//
// In the case of multiple bits being used at the same time we make
// a distinction between the conditions in AND_conditions_mask and
// the other bits, OR_conditions_mask.
//
// As long as we're waiting for any bit in AND_conditions_mask
// the task may not run. All the bits together in the
// OR_conditions_mask act as a single AND bit, as in:
//
//   running = running1 && running2 && running3 && (running4 || running5 || running 6)
//
// where 1, 2 and 3 are in the AND_conditions_mask and
// 4, 5 and 6 in the OR_conditions_mask.
//
// Every time the task starts running again, all previous
// waits and signals are irrelevant (because we are no longer
// waiting for them), but still being used to determine if
// we're idle or not. Therefore, whenever a signal is received
// for a bit in the OR_conditions_mask, every bit in that mask
// is set to non-idle.
//
// Finally, it is of course possible that a signal is received
// during the run of a multiplex_impl for a bit that we're not
// waiting for yet. This also has to work. Hence the functionality
// of wait() is much like that of signal(): if we first get
// a signal(bit) followed by a wait(bit) then this must result
// in the exact same state as when the order was swapped.
//
// Nevertheless a different state can result when combining
// multiple OR conditions. For example:
//
//     start_obtaining(3);      // Results in signal(1) and signal(2) to be called, eventually.
//     wait(3);                 // Wake up at the first signal.
//     break;
//
// can lead to
//
//     signal(2);               // idle=0; skip_wait=2
//     signal(1);               // idle=0; skip_wait=3
//     wait(3);                 // idle=0; skip_wait=0
//
// but also to
//
//     signal(2);               // idle=0; skip_wait=2
//     wait(3);                 // idle=0; skip_wait=0
//     signal(1);               // idle=0; skip_wait=1
//
// The reason for the difference is that in the second
// case, because of the match with signal(2) the wait(3)
// acts the same as the trivial:
//
//     wait(2);                 // idle=1; skip_wait=0
//     signal(2);               // idle=0; skip_wait=0
//
// That is, we are not idle. Note how this is, and should be, the same result as
//
//     signal(2);               // idle=0; skip_wait=2
//     wait(2);                 // idle=0; skip_wait=0
//
// If at this point wait(1) would be called then the
// result would be idle=1; skip_wait=0. You are not
// allowed to call wait twice on a row, but the task
// is still running, so it could happen in a new state/run:
//
//     signal(2);               // idle=0; skip_wait=2
//     wait(2);                 // idle=0; skip_wait=0
//     wait(1);                 // idle=1; skip_wait=0
//     signal(1);               // idle=0; skip_wait=0
//
// However, the wait(3) means: wait for signal 1 OR signal 2,
// and reset ALL idle bits (from the OR_conditions_mask) when
// any of the signals is received.
//
// Therefore the wait(1) part of it gets lost and the
// later signal(1) would combine with a subsequent wait(1),
// not with the wait(3) that it was intended for.
//
// This is why you should never reuse the condition bits
// of the OR_conditions_mask in a wait when having used
// them in a wait with more than one bit set. In general
// it is highly advised to simply have every wait() in
// the task use different condition bits period.
//
//
// The above requires three states per bit:
//
// a) wait() called but not signal() received yet (idle).
// b) One (or more) signal() received but no wait() called yet (skip_wait).
// c) None of the above.
//
// This requires two bits, and hence two masks for everything
// together: `idle` is a mask that indicates if any condition
// bit is `idle` (wait called, but no signal).
//
// `skip_wait` has a bit set when signal() was received but
// no wait() was called yet.
//
void AIStatefulTask::wait(condition_type conditions)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::wait(" << std::hex << conditions << std::dec << ") [" << (void*)this << "]");
  // The bits in AND_conditions_mask are reserved, don't use them.
  ASSERT(!(conditions & AND_conditions_mask));
#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // wait() may only be called from multiplex_impl().
    ASSERT(state_r->base_state == bs_multiplex);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
  // wait() following set_state() cancels the reason to run because of the call to set_state.
  mDebugSetStatePending = false;
#endif
  {
    sub_state_type::wat sub_state_w(mSubState);
    // As wait() may only be called from within the stateful task, it should never happen that the task is already idle.
    // Only call wait() from inside multiplex_impl() and always immediately leave that function afterwards (do not call
    // wait() twice on a row).
    // Mask OR_conditions_mask because it is possible that wait_AND() was already called this run.
    ASSERT(!(sub_state_w->idle & OR_conditions_mask));
#if CW_DEBUG
    // Mark that we at least attempted to go idle.
    sub_state_w->wait_called = true;
#endif

    // Determine if we must go idle.

    // Mark that we are now also waiting for the condition corresponding to conditions.
    sub_state_w->idle |= conditions & ~sub_state_w->skip_wait;
    // Reset the masked bits in skip_wait.
    sub_state_w->skip_wait &= ~conditions;

    // Detect if this wait was combined with a signal in the OR_conditions_mask.
    condition_type mask = (conditions & ~sub_state_w->idle & OR_conditions_mask) ? OR_conditions_mask : 0;
    // Reset all OR_conditions_mask bits if that was the case.
    sub_state_w->idle &= ~mask;

    // Not sleeping (anymore).
    mSleep = 0;
#if CW_DEBUG
    // From this moment.
    mDebugSignalPending = !sub_state_w->idle;
#endif
  }
}

void AIStatefulTask::wait_AND(condition_type conditions)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::wait_AND(" << std::hex << conditions << std::dec << ") [" << (void*)this << "]");
  // Only use the reserved bits in AND_conditions_mask here.
  ASSERT(conditions && (conditions & AND_conditions_mask) == conditions);
#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // wait_AND() may only be called from (a function called from) multiplex_impl().
    ASSERT(state_r->base_state == bs_multiplex);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
  // wait_AND() following set_state() cancels the reason to run because of the call to set_state.
  mDebugSetStatePending = false;
#endif
  {
    sub_state_type::wat sub_state_w(mSubState);
    // It should be impossible that we are idle since we can only get here from inside multiplex_impl.
    // See the comments in AIStatefulTask::add_task_to_thread_pool.
    ASSERT(!(sub_state_w->idle & OR_conditions_mask));
#if CW_DEBUG
    // Mark that we at least attempted to go idle.
    sub_state_w->wait_called = true;
#endif

    // Determine if we must go idle.

    // Mark that we are now (also) waiting for the condition corresponding to conditions.
    sub_state_w->idle |= conditions & ~sub_state_w->skip_wait;
    // Reset the maskeds bit in skip_wait.
    sub_state_w->skip_wait &= ~conditions;

    // Not sleeping (anymore).
    mSleep = 0;
#if CW_DEBUG
    // From this moment.
    mDebugSignalPending = !sub_state_w->idle;
#endif
  }
}

// Go idle until wait_condition() becomes true. For this to work, signal(condition) must be called
// each time that something changed that might cause wait_condition() to become true, most notably
// signal(condition) must be called at least once after wait_condition() has become true.
void AIStatefulTask::wait_until(std::function<bool()> const& wait_condition, condition_type conditions)
{
  if (!wait_condition())
  {
    {
      multiplex_state_type::wat state_w(mState);
      state_w->wait_condition = wait_condition;
      state_w->conditions = conditions;
    }
    wait(conditions);
  }
}

// This function causes the task to do at least one full run of multiplex(), provided we are
// currently idle as a result of a call to wait() with the same condition.
// Returns true if the stateful task was unblocked, false if it was already unblocked.
bool AIStatefulTask::signal(condition_type condition)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::signal(" << std::hex << condition << std::dec << ") [" << (void*)this << "]");
  // It is not allowed to call this function with an empty mask.
  ASSERT(condition);
  {
    sub_state_type::wat sub_state_w(mSubState);
    // Remember if we're idle at this moment.
    condition_type prev_idle = sub_state_w->idle;
    // Set skip_wait if we didn't already see a wait.
    sub_state_w->skip_wait |= condition & ~prev_idle;
    // Did we wake up a condition in the OR_conditions_mask? Then pretend we received a signal for all of them.
    condition |= (condition & ~sub_state_w->skip_wait & OR_conditions_mask) ? OR_conditions_mask : 0;
    // If a wait WAS seen before then now we're no longer idle for condition.
    sub_state_w->idle &= ~condition;

    // Did this signal NOT cause us to wake up?
    if (!prev_idle || sub_state_w->idle)
    {
#ifdef CWDEBUG
      if (mSMDebug)
      {
        if (prev_idle)
          Dout(dc::statefultask, "Task is not waiting for " << std::hex << condition << std::dec <<
              " (idle == " << std::hex << sub_state_w->idle << std::dec << "). Signal queued for possible subsequent wait.");
        else
          Dout(dc::statefultask, "Task is not waiting. Signal queued for possible subsequent wait.");
      }
#endif
      return false;
    }
#if CW_DEBUG
    mDebugSignalPending = sub_state_w->wait_called;
#endif
    // Mark that a re-entry of multiplex() is necessary.
    sub_state_w->need_run = true;
  }
  if (!mMultiplexMutex.is_self_locked())
    multiplex(schedule_run);
  return true;
}

void AIStatefulTask::abort()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::abort() [" << (void*)this << "]");
  bool is_waiting = false;
  {
    multiplex_state_type::rat state_r(mState);
    sub_state_type::wat sub_state_w(mSubState);
    // Mark that we are aborted, iff we didn't already finish.
    sub_state_w->aborted = !sub_state_w->finished;
    // Schedule a new run when this task is waiting.
    is_waiting = state_r->base_state == bs_multiplex && sub_state_w->idle;
    // No longer say we woke up when signal() is called.
    if (sub_state_w->idle)
    {
      Dout(dc::statefultask(mSMDebug), "Removing block on mask " << std::hex << sub_state_w->idle << std::dec);
      sub_state_w->idle = 0;
    }
    // Mark that a re-entry of multiplex() is necessary.
    sub_state_w->need_run = true;
  }
  if (is_waiting && !mMultiplexMutex.is_self_locked())
    multiplex(insert_abort);
  // Block until the current run finished.
  if (!mRunMutex.try_lock())
  {
    Dout(dc::warning, "AIStatefulTask::abort() blocks because the stateful task is still executing code in another thread.");
    mRunMutex.lock();
  }
  mRunMutex.unlock();
#if CW_DEBUG
  // When abort() returns, it may never run again.
  mDebugAborted = true;
#endif
}

void AIStatefulTask::finish()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::finish() [" << (void*)this << "]");
#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // finish() may only be called from multiplex_impl().
    ASSERT(state_r->base_state == bs_multiplex);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
#endif
  {
    sub_state_type::wat sub_state_w(mSubState);
    // finish() should not be called when idle.
    ASSERT(!sub_state_w->idle);
    // But reset idle to stop subsequent calls to signal() from calling multiplex().
    sub_state_w->idle = 0;
    // Mark that we are finished.
    sub_state_w->finished = true;
  }
}

void AIStatefulTask::yield()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::yield() [" << (void*)this << "]");
#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // yield() may only be called from multiplex_impl().
    ASSERT(state_r->base_state == bs_multiplex);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
#endif
  // Indicate we should leave mainloop().
  mYield = true;
}

void AIStatefulTask::target(Handler handler)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::target(" << handler << ") [" << (void*)this << "]");
  // This is not possible. Do not specify Handler::immediate as (yield) target.
  // Use an actual AIEngine or AIQueueHandle. To turn off a target, use target(Handler::idle).
  ASSERT(!handler.is_immediate());
#if CW_DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
#endif
  mTargetHandler = handler;
}

void AIStatefulTask::yield(Handler handler)
{
  // Only yield to an actual AIEngine or AIQueueHandle.
  // To turn off a target, use target(Handler::idle).
  ASSERT(handler);
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::yield(" << handler << ") [" << (void*)this << "]");
  target(handler);
  yield();
}

bool AIStatefulTask::yield_if_not(Handler handler)
{
  if (handler && !(multiplex_state_type::rat(mState)->current_handler == handler))
  {
    yield(handler);
    return true;
  }
  return false;
}

void AIStatefulTask::yield_frame(AIEngine* engine, unsigned int frames)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::yield_frame(" << engine->name() << ", " << frames << ") [" << (void*)this << "]");
  mSleep = -static_cast<AIEngine::clock_type::rep>(frames);       // Frames are stored as a negative number.
  // Sleeping is always done from an engine with mMaxDuration set.
  ASSERT(engine->hasMaxDuration());
  yield(engine);
}

void AIStatefulTask::yield_ms(AIEngine* engine, unsigned int ms)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::yield_ms(" << engine->name() << ", " << ms << ") [" << (void*)this << "]");
  AIEngine::duration_type sleep_duration = std::chrono::duration_cast<AIEngine::duration_type>(std::chrono::duration<unsigned int, std::milli>(ms));
  mSleep = (AIEngine::clock_type::now() + sleep_duration).time_since_epoch().count();
  // Sleeping is always done from an engine with mMaxDuration set.
  ASSERT(engine->hasMaxDuration());
  yield(engine);
}

char const* AIStatefulTask::state_str(base_state_type state)
{
  switch (state)
  {
    AI_CASE_RETURN(bs_reset);
    AI_CASE_RETURN(bs_initialize);
    AI_CASE_RETURN(bs_multiplex);
    AI_CASE_RETURN(bs_abort);
    AI_CASE_RETURN(bs_finish);
    AI_CASE_RETURN(bs_callback);
    AI_CASE_RETURN(bs_killed);
  }
  ASSERT(false);
  return "UNKNOWN BASE STATE";
}

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
channel_ct statefultask("STATEFULTASK");
NAMESPACE_DEBUG_CHANNELS_END
#endif
