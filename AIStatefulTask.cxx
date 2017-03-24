/**
 * @file
 * @brief Implementation of AIStatefulTask.
 *
 * Copyright (C) 2010 - 2013, 2017  Carlo Wood.
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

#include "sys.h"
#include "AIEngine.h"

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
//       | (idle)  |    Idle until signalled() is called.
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

// Every stateful task is (indirectly) derived from AIStatefulTask.
// For example:

#ifdef EXAMPLE_CODE     // undefined

class HelloWorld : public AIStatefulTask {
  protected:
    // The base class of this task.
    typedef AIStatefulTask direct_base_type;

    // The different states of the task.
    enum hello_world_state_type {
      HelloWorld_start = direct_base_type::max_state,
      HelloWorld_done,
    };
  public:
    static state_type const max_state = HelloWorld_done + 1;    // One beyond the largest state.

  public:
    // The derived class must have a default constructor.
    HelloWorld();

  protected:
    // The destructor must be protected.
    /*virtual*/ ~HelloWorld();

  protected:
    // The following virtual functions must be implemented:

    // Handle initializing the object.
    /*virtual*/ void initialize_impl();

    // Handle mRunState.
    /*virtual*/ void multiplex_impl(state_type run_state);

    // Handle aborting from current bs_multiplex state (the default AIStatefulTask::abort_impl() does nothing).
    /*virtual*/ void abort_impl();

    // Handle cleaning up from initialization (or post abort) state (the default AIStatefulTask::finish_impl() does nothing).
    /*virtual*/ void finish_impl();

    // Return human readable string for run_state.
    /*virtual*/ char const* state_str_impl(state_type run_state) const;
};

// In the .cpp file:

char const* HelloWorld::state_str_impl(state_type run_state) const
{
  switch(run_state)
  {
    // A complete listing of hello_world_state_type.
    AI_CASE_RETURN(HelloWorld_start);
    AI_CASE_RETURN(HelloWorld_done);
  }
#if directly_derived_from_AIStatefulTask
  ASSERT(false);
  return "UNKNOWN STATE";
#else
  ASSERT(run_state < direct_base_type::max_state);
  return direct_base_type::state_str_impl(run_state);
#endif
}

#endif // EXAMPLE_CODE


//==================================================================
// Life cycle: creation, initialization, running and destruction

// Any thread may create a stateful task object, initialize it by calling
// it's initializing member function and call one of the 'run' methods,
// which might or might not immediately start to execute the task.

#ifdef EXAMPLE_CODE
HelloWorld* hello_world = new HelloWorld;
hello_world->init(...);         // A custom initialization function.
hello_world->run(...);          // One of the run() functions.
// hello_world might be destructed here.
// You can avoid possible destruction by using an boost::intrusive_ptr<HelloWorld>
// instead of HelloWorld*.
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
// If wait(condition) was called, and the condition.running() returns false,
//   then multiplex_impl() will not be called again until condition.signal()
//   is called from outside.
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
// - wait(condition)            --> Go idle (do nothing until signalled() is called), however if condition.running()
//                                  returns true, then multiplex_impl shall be reentered immediately upon return.
// - finish()                   --> Disables any scheduled runs.
//                              --> finish_impl --> [optional] kill()
//                              --> call back
//                              --> [optional] delete
//                              --> [optional] reset, upon return from multiplex_impl, call initialize_impl and start again at the top of multiplex.
// - yield([engine])            --> give CPU to other tasks before running again, run next from a stateful task engine.
//                                  If no engine is passed, the task will run in it's default engine (as set during construction).
// - yield_frame()/yield_ms()   --> yield(&gMainThreadEngine)
//
// the following function may be called from multiplex_impl() of any task (and thus by any thread):
//
// - abort()                    --> abort_impl
//                              --> finish()
//
// while the following functions may be called from anywhere (and any thread):
//
// - signalled(condition)       --> if 'condition' matches the condition passed to the last call to wait() then schedule a run.
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
  switch(event)
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
  return state_r->base_state == bs_abort || ( state_r->base_state == bs_multiplex && sub_state_type::crat(mSubState)->idle);
}

void AIStatefulTask::multiplex(event_type event, AIEngine* engine)
{
  // If this fails then you are using a pointer to a stateful task instead of an boost::intrusive_ptr<AIStatefulTask>.
  ASSERT(event == initial_run || ref_count() > 0);

  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::multiplex(" << event_str(event) << ") [" << (void*)this << "]");

  base_state_type state;
  state_type run_state;

  // Critical area of mState.
  {
    multiplex_state_type::rat state_r(mState);

    // This would be an almost impossible race condition.
    if (AI_UNLIKELY(event == insert_abort && state_r->base_state != bs_multiplex))
    {
      Dout(dc::statefultask(mSMDebug), "Leaving because the task finished in the meantime [" << (void*)this << "]");
      return;
    }

    if (event == normal_run && engine != state_r->current_engine)
    {
      Dout(dc::statefultask(mSMDebug), "Leaving because current_engine isn't equal to calling engine [" << (void*)this << "]");
      return;
    }

    // multiplex(schedule_run) is only called from signal(idle_mask) provided that
    // mSubState.idle & idle_mask is non-zero, which is never true when idle is set to
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
    ASSERT(!mMultiplexMutex.self_locked());    // We may never enter recursively!
    if (!mMultiplexMutex.try_lock())
    {
      // This just should never happen; a call to run() should always set the base state beyond bs_reset.
      ASSERT(event != initial_run);
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
    run_state = begin_loop((state = state_r->base_state));
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
        Dout(dc::statefultask(mSMDebug), "Running state bs_multiplex / " << state_str_impl(run_state) << " [" << (void*)this << "]");
      else
        Dout(dc::statefultask(mSMDebug), "Running state " << state_str(state) << " [" << (void*)this << "]");

      // This debug code checks that each task steps precisely through each of it's states correctly.
      if (state != bs_reset)
      {
        switch(mDebugLastState)
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
#ifdef DEBUG
      mDebugLastState = state;
      // Make sure we only call ref() once and in balance with unref().
      if (state == bs_initialize)
      {
        // This -- and call to ref() (and the test when we're about to call unref()) -- is all done in the critical area of mMultiplexMutex.
        ASSERT(!mDebugRefCalled);
        mDebugRefCalled = true;
      }
#endif
      switch(state)
      {
        case bs_reset:
          // We're just being kick started to get into the right thread
          // (possibly for the second time when a late abort was detected, but that's ok: we do nothing here).
          break;
        case bs_initialize:
          intrusive_ptr_add_ref(this);
          initialize_impl();
          break;
        case bs_multiplex:
          ASSERT(!mDebugAborted);
          multiplex_impl(run_state);
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
          // This should already be have been set to nullptr to indicate that we want to be removed from that engine.
          ASSERT(!multiplex_state_type::rat(mState)->current_engine);
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
          switch(state)
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
                ASSERT(!(need_new_run && !mYieldEngine && sub_state_r->run_state == run_state &&
                       !(sub_state_r->aborted ||        // abort was called.
                         sub_state_r->finished ||       // finish was called.
                         sub_state_r->wait_called)));   // wait was called.
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

      // Figure out in which engine we should run.
      AIEngine* engine = mYieldEngine ? mYieldEngine : (state_w->current_engine ? state_w->current_engine : mDefaultEngine);
      // And the current engine we're running in.
      AIEngine* current_engine = (event == normal_run) ? state_w->current_engine : nullptr;

      // Immediately run again if yield() wasn't called and it's OK to run in this thread.
      // Note that when it's OK to run in any engine (mDefaultEngine is nullptr) then the last
      // compare is also true when current_engine == nullptr.
      keep_looping = need_new_run && !mYieldEngine && engine == current_engine;
      mYieldEngine = nullptr;

      Dout(dc::statefultask(mSMDebug), (!need_new_run ? "No need to run" : !keep_looping ? "Need to run, adding to engine" : "Need to run, will run right now") << " [" << (void*)this << "]");

      if (keep_looping)
      {
        // Start a new loop.
        run_state = begin_loop((state = state_w->base_state));
        event = normal_run;
      }
      else
      {
        if (need_new_run)
        {
          // Add us to an engine if necessary.
          if (engine != state_w->current_engine)
          {
            // Mark that we want to run in this engine, and at the same time, that we don't want to run in the previous one.
            state_w->current_engine = engine;
            // Actually add the task to the engine; engine can't be nullptr here: it can only be nullptr if mDefaultEngine is nullptr.
            engine->add(this);
          }
#ifdef DEBUG
          // We are leaving the loop, but we're not idle. The task should re-enter the loop again.
          mDebugShouldRun = true;
#endif
        }
        else
        {
          // Remove this task from any engine,
          // causing the engine to remove us.
          state_w->current_engine = nullptr;
        }

#ifdef DEBUG
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
    intrusive_ptr_release(this);
  }
}

AIStatefulTask::state_type AIStatefulTask::begin_loop(base_state_type base_state)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::begin_loop(" << state_str(base_state) << ") [" << (void*)this << "]");

  sub_state_type::wat sub_state_w(mSubState);
  // Mark that we're about to honor all previous run requests.
  sub_state_w->need_run = false;
  // Mark that we're currently not idle and wait() wasn't called (yet).
  sub_state_w->wait_called = false;

#ifdef DEBUG
  // Mark that we're running the loop.
  mThreadId = std::this_thread::get_id();
  // This point marks handling wait() with pending signal().
  mDebugShouldRun |= mDebugSignalPending;
  mDebugSignalPending = false;
#endif

  // Make a copy of the state that we're about to run.
  return sub_state_w->run_state;
}

void AIStatefulTask::run(AIStatefulTask* parent, idle_mask_type condition, on_abort_st on_abort, AIEngine* default_engine)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::run(" <<
      (void*)parent << ", " << std::hex << condition << std::dec <<
      ", on_abort = " << ((on_abort == abort_parent) ? "abort_parent" : (on_abort == signal_parent) ? "signal_parent" : "do_nothing") <<
      ", default_engine = " << (default_engine ? default_engine->name() : "nullptr") << ") [" << (void*)this << "]");

#ifdef DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // Can only be run when in one of these states.
    ASSERT(state_r->base_state == bs_reset || state_r->base_state == bs_finish || state_r->base_state == bs_callback);
    // Must be the first time we're being run, or we must be called from finish_impl or a callback function.
    ASSERT(!(state_r->base_state == bs_reset && (mParent || mCallback)));
  }
#endif

  // Store the requested default engine.
  mDefaultEngine = default_engine;

  // Initialize sleep timer.
  mSleep = 0;

  // Allow nullptr to be passed as parent to signal that we want to reuse the old one.
  if (parent)
  {
    mParent = parent;
    // In that case remove any old callback!
    if (mCallback)
    {
      delete mCallback;
      mCallback = nullptr;
    }

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

void AIStatefulTask::run(callback_type::signal_type::slot_type const& slot, AIEngine* default_engine)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::run(<slot>, default_engine = " << default_engine->name() << ") [" << (void*)this << "]");

#ifdef DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // Can only be run when in one of these states.
    ASSERT(state_r->base_state == bs_reset || state_r->base_state == bs_finish || state_r->base_state == bs_callback);
    // Must be the first time we're being run, or we must be called from finish_impl or a callback function.
    ASSERT(!(state_r->base_state == bs_reset && (mParent || mCallback)));
  }
#endif

  // Store the requested default engine.
  mDefaultEngine = default_engine;

  // Initialize sleep timer.
  mSleep = 0;

  // Clean up any old callbacks.
  mParent = nullptr;
  if (mCallback)
  {
    delete mCallback;
    mCallback = nullptr;
  }

  // Create new call back.
  mCallback = new callback_type(slot);

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
    mCallback->callback(!aborted);
    if (multiplex_state_type::rat(mState)->base_state != bs_reset)
    {
      delete mCallback;
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

void AIStatefulTask::force_killed()
{
  multiplex_state_type::wat(mState)->base_state = bs_killed;
}

void AIStatefulTask::kill()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::kill() [" << (void*)this << "]");
#ifdef DEBUG
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
#ifdef DEBUG
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
#ifdef DEBUG
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
    // It should never happen that set_state() is called while we're idle.
    ASSERT(!sub_state_w->idle);
    // Force current state to the requested state.
    sub_state_w->run_state = new_state;
#ifdef DEBUG
    // We should run. This can only be cancelled by a call to wait().
    mDebugSetStatePending = true;
#endif
  }
}

void AIStatefulTask::wait(idle_mask_type idle_bit)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::wait(" << std::hex << idle_bit << std::dec << ") [" << (void*)this << "]");
#ifdef DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // wait() may only be called multiplex_impl().
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
    ASSERT(!sub_state_w->idle);
    // Mark that we at least attempted to go idle.
    sub_state_w->wait_called = true;

    // Determine if we must go idle.

    // Copy bits from m_skip_idle to m_not_idle.
    sub_state_w->not_idle &= ~idle_bit;     				// Reset the masked bit.
    sub_state_w->not_idle |= sub_state_w->skip_idle & idle_bit;         // Then set the masked bit if it is set in m_skip_idle.
    // Reset the masked bit in m_skip_idle.
    sub_state_w->skip_idle &= ~idle_bit;
    // Mark that we are waiting for the condition corresponding to idle_bit.
    sub_state_w->idle = ~sub_state_w->not_idle & idle_bit;

    // Not sleeping (anymore).
    mSleep = 0;
#ifdef DEBUG
    // From this moment.
    mDebugSignalPending = !sub_state_w->idle;
#endif
  }
}

// This function causes the task to do at least one full run of multiplex(), provided we are
// currently idle as a result of a call to wait() with the same idle_bit.
// Returns true if the stateful task was unblocked, false if it was already unblocked.
bool AIStatefulTask::signal(idle_mask_type idle_mask)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::signal(" << std::hex << idle_mask << std::dec << ") [" << (void*)this << "]");
  // It is not allowed to call this function with an empty mask.
  ASSERT(idle_mask);
  {
    sub_state_type::wat sub_state_w(mSubState);
    // Copy bits from not_idle to skip_idle.
    sub_state_w->skip_idle &= ~idle_mask;                		// Reset the masked bits.
    sub_state_w->skip_idle |= sub_state_w->not_idle & idle_mask;	// Then set masked bits that are set in m_not_idle.
    // Set the masked bits in not_idle;
    sub_state_w->not_idle |= idle_mask;
    // Test if we are idle or not.
    if (!(sub_state_w->idle & idle_mask))
    {
      Dout(dc::statefultask(mSMDebug), "Ignoring because idle == " << std::hex << sub_state_w->idle << std::dec);
      return false;
    }
#ifdef DEBUG
    mDebugSignalPending = sub_state_w->wait_called;
#endif
    // Unblock this task.
    sub_state_w->idle = 0;
    // Mark that a re-entry of multiplex() is necessary.
    sub_state_w->need_run = true;
  }
  if (!mMultiplexMutex.self_locked())
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
    // No longer say we woke up when signal() is called.
    if (sub_state_w->idle)
    {
      Dout(dc::statefultask(mSMDebug), "Removing block on mask " << std::hex << sub_state_w->idle << std::dec);
      sub_state_w->idle = 0;
    }
    // Mark that a re-entry of multiplex() is necessary.
    sub_state_w->need_run = true;
    // Schedule a new run when this task is waiting.
    is_waiting = state_r->base_state == bs_multiplex && sub_state_w->idle;
  }
  if (is_waiting && !mMultiplexMutex.self_locked())
    multiplex(insert_abort);
  // Block until the current run finished.
  if (!mRunMutex.try_lock())
  {
    Dout(dc::warning, "AIStatefulTask::abort() blocks because the stateful task is still executing code in another thread.");
    mRunMutex.lock();
  }
  mRunMutex.unlock();
#ifdef DEBUG
  // When abort() returns, it may never run again.
  mDebugAborted = true;
#endif
}

void AIStatefulTask::finish()
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::finish() [" << (void*)this << "]");
#ifdef DEBUG
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
  multiplex_state_type::rat state_r(mState);
  // yield() may only be called from multiplex_impl().
  ASSERT(state_r->base_state == bs_multiplex);
  // May only be called by the thread that is holding mMultiplexMutex.
  ASSERT(mThreadId == std::this_thread::get_id());
  // Set mYieldEngine to the best non-NUL value.
  mYieldEngine = state_r->current_engine ? state_r->current_engine : (mDefaultEngine ? mDefaultEngine : &gAuxiliaryThreadEngine);
}

void AIStatefulTask::yield(AIEngine* engine)
{
  ASSERT(engine);
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::yield(" << engine->name() << ") [" << (void*)this << "]");
#ifdef DEBUG
  {
    multiplex_state_type::rat state_r(mState);
    // yield() may only be called from multiplex_impl().
    ASSERT(state_r->base_state == bs_multiplex);
    // May only be called by the thread that is holding mMultiplexMutex.
    ASSERT(mThreadId == std::this_thread::get_id());
  }
#endif
  mYieldEngine = engine;
}

bool AIStatefulTask::yield_if_not(AIEngine* engine)
{
  if (engine && multiplex_state_type::rat(mState)->current_engine != engine)
  {
    yield(engine);
    return true;
  }
  return false;
}

void AIStatefulTask::yield_frame(unsigned int frames)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::yield_frame(" << frames << ") [" << (void*)this << "]");
  mSleep = -static_cast<AIEngine::clock_type::rep>(frames);       // Frames are stored as a negative number.
  // Sleeping is always done from the main thread.
  yield(&gMainThreadEngine);
}

void AIStatefulTask::yield_ms(unsigned int ms)
{
  DoutEntering(dc::statefultask(mSMDebug), "AIStatefulTask::yield_ms(" << ms << ") [" << (void*)this << "]");
  AIEngine::duration_type sleep_duration = std::chrono::duration_cast<AIEngine::duration_type>(std::chrono::duration<unsigned int, std::milli>(ms));
  mSleep = (AIEngine::clock_type::now() + sleep_duration).time_since_epoch().count();
  // Sleeping is always done from the main thread.
  yield(&gMainThreadEngine);
}

char const* AIStatefulTask::state_str(base_state_type state)
{
  switch(state)
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

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
channel_ct statefultask("STATEFULTASK");
NAMESPACE_DEBUG_CHANNELS_END
#endif
