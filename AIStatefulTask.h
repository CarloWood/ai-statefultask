/**
 * @file
 * @brief Declaration of base class AIStatefulTask.
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

#pragma once

#include "utils/AIRefCount.h"
#include "threadsafe/aithreadsafe.h"
#include "debug.h"
#include <list>
#include <chrono>
#include <boost/signals2.hpp>

class AIConditionBase;
class AIEngine;

extern AIEngine gMainThreadEngine;
extern AIEngine gAuxiliaryThreadEngine;

class AIStatefulTask : public AIRefCount
{
  public:
    typedef uint32_t state_type;        //!< The type of run_state.

  protected:
    // The type of event that causes multiplex() to be called.
    enum event_type {
      initial_run,
      schedule_run,
      normal_run,
      insert_abort
    };
    // The type of mState
    enum base_state_type {
      bs_reset,                 // Idle state before run() is called. Reference count is zero (except for a possible external boost::intrusive_ptr).
      bs_initialize,            // State after run() and before/during initialize_impl().
      bs_multiplex,             // State after initialize_impl() before finish() or abort().
      bs_abort,
      bs_finish,
      bs_callback,
      bs_killed
    };
  public:
    static state_type const max_state = bs_killed + 1;

  protected:
    struct multiplex_state_st {
      base_state_type base_state;
      AIEngine* current_engine;         // Current engine.
      multiplex_state_st(void) : base_state(bs_reset), current_engine(nullptr) { }
    };
    struct sub_state_st {
      state_type run_state;
      state_type advance_state;
      AIConditionBase* blocked;
      bool reset;
      bool need_run;
      bool idle;
      bool skip_idle;
      bool aborted;
      bool finished;
    };

  private:
    // Base state.
    typedef aithreadsafe::Wrapper<multiplex_state_st, aithreadsafe::policy::Primitive<std::mutex>> multiplex_state_type;
    multiplex_state_type mState;

  protected:
    // Sub state.
    typedef aithreadsafe::Wrapper<sub_state_st, aithreadsafe::policy::Primitive<std::mutex>> sub_state_type;
    sub_state_type mSubState;

  private:
    // Mutex protecting everything below and making sure only one thread runs the task at a time.
    std::mutex mMultiplexMutex;
    // Mutex that is locked while calling *_impl() functions and the call back.
    std::mutex mRunMutex;

    typedef std::chrono::steady_clock clock_type;
    typedef clock_type::duration duration_type;

    clock_type::rep mSleep;   //!< Non-zero while the task is sleeping. Negative means frames, positive means clock periods.

    // Callback facilities.
    // From within an other stateful task:
    boost::intrusive_ptr<AIStatefulTask> mParent;       // The parent object that started this task, or nullptr if there isn't any.
    state_type mNewParentState;                         // The state at which the parent should continue upon a successful finish.
    bool mAbortParent;                                  // If true, abort parent on abort(). Otherwise continue as normal.
    bool mOnAbortSignalParent;                          // If true and mAbortParent is false, change state of parent even on abort.
    // From outside a stateful task:
    struct callback_type {
      typedef boost::signals2::signal<void (bool)> signal_type;
      callback_type(signal_type::slot_type const& slot) { connection = signal.connect(slot); }
      ~callback_type() { connection.disconnect(); }
      void callback(bool success) const { signal(success); }
      private:
      boost::signals2::connection connection;
      signal_type signal;
    };
    callback_type* mCallback;           // Pointer to signal/connection, or nullptr when not connected.

    // Engine stuff.
    AIEngine* mDefaultEngine;           // Default engine.
    AIEngine* mYieldEngine;             // Requested engine.

    thread_local static AIStatefulTask const* tl_running_task;  // The task that a given thread is running (executing AIStatefulTask::multiplex), if any.

#ifdef DEBUG
    // Debug stuff.
    base_state_type mDebugLastState;    // The previous state that multiplex() had a normal run with.
    bool mDebugShouldRun;               // Set if we found evidence that we should indeed call multiplex_impl().
    bool mDebugAborted;                 // True when abort() was called.
    bool mDebugContPending;             // True while cont() was called by not handled yet.
    bool mDebugSetStatePending;         // True while set_state() was called by not handled yet.
    bool mDebugAdvanceStatePending;     // True while advance_state() was called by not handled yet.
    bool mDebugRefCalled;               // True when ref() is called (or will be called within the critial area of mMultiplexMutex).
#endif
#ifdef CWDEBUG
  protected:
    bool mSMDebug;                      // Print debug output only when true.
#endif
  private:
    duration_type mDuration;  // Total time spent running in the main thread.

  public:
    AIStatefulTask(DEBUG_ONLY(bool debug)) : mCallback(nullptr), mDefaultEngine(nullptr), mYieldEngine(nullptr),
#ifdef DEBUG
    mDebugLastState(bs_killed), mDebugShouldRun(false), mDebugAborted(false), mDebugContPending(false),
    mDebugSetStatePending(false), mDebugAdvanceStatePending(false), mDebugRefCalled(false),
#endif
#ifdef CWDEBUG
    mSMDebug(debug),
#endif
    mDuration(duration_type::zero())
    { }

  protected:
    // The user should call finish() (or abort(), or kill() from the call back when finish_impl() calls run()),
    // not delete a class derived from AIStatefulTask directly. Deleting it directly before calling run() is
    // ok however.
    virtual ~AIStatefulTask()
    {
#ifdef DEBUG
      base_state_type state = multiplex_state_type::rat(mState)->base_state;
      ASSERT(state == bs_killed || state == bs_reset);
#endif
    }

  public:
    // These functions may be called directly after creation, or from within finish_impl(), or from the call back function.
    void run(AIStatefulTask* parent, state_type new_parent_state, bool abort_parent = true, bool on_abort_signal_parent = true, AIEngine* default_engine = &gMainThreadEngine);
    void run(callback_type::signal_type::slot_type const& slot, AIEngine* default_engine = &gMainThreadEngine);
    void run(void) { run(nullptr, 0, false, true, mDefaultEngine); }

    // This function may only be called from the call back function (and cancels a call to run() from finish_impl()).
    void kill(void);

  protected:
    // This function can be called from initialize_impl() and multiplex_impl() (both called from within multiplex()).
    void set_state(state_type new_state);       // Run this state the NEXT loop.
    // These functions can only be called from within multiplex_impl().
    void idle(void);                            // Go idle unless cont() or advance_state() were called since the start of the current loop, or until they are called.
    void wait(AIConditionBase& condition);      // The same as idle(), but wake up when AICondition<T>::signal() is called.
    void finish(void);                          // Mark that the task finished and schedule the call back.
    void yield(void);                           // Yield to give CPU to other tasks, but do not go idle.
    void yield(AIEngine* engine);               // Yield to give CPU to other tasks, but do not go idle. Continue running from engine 'engine'.
    void yield_frame(unsigned int frames);      // Run from the main-thread engine after at least 'frames' frames have passed.
    void yield_ms(unsigned int ms);             // Run from the main-thread engine after roughly 'ms' miliseconds have passed.
    bool yield_if_not(AIEngine* engine);        // Do not really yield, unless the current engine is not 'engine'. Returns true if it switched engine.

  public:
    // This function can be called from multiplex_imp(), but also by a child task and
    // therefore by any thread. The child task should use an boost::intrusive_ptr<AIStatefulTask>
    // to access this task.
    void abort(void);                           // Abort the task (unsuccessful finish).

    // These are the only three functions that can be called by any thread at any moment.
    // Those threads should use an boost::intrusive_ptr<AIStatefulTask> to access this task.
    void cont(void);                            // Guarantee at least one full run of multiplex() after this function is called. Cancels the last call to idle().
    void advance_state(state_type new_state);   // Guarantee at least one full run of multiplex() after this function is called
    // iff new_state is larger than the last state that was processed.
    bool signalled(void);                       // Call cont() iff this task is still blocked after a call to wait(). Returns false if it already unblocked.

  public:
    // Accessors.

    // Return true if the derived class is running (also when we are idle).
    bool running(void) const { return multiplex_state_type::crat(mState)->base_state == bs_multiplex; }
    // Return true if the derived class is running and idle.
    bool waiting(void) const
    {
      multiplex_state_type::crat state_r(mState);
      return state_r->base_state == bs_multiplex && sub_state_type::crat(mSubState)->idle;
    }
    // Return true if the derived class is running and idle or already being aborted.
    bool waiting_or_aborting(void) const
    {
      multiplex_state_type::crat state_r(mState);
      return state_r->base_state == bs_abort || ( state_r->base_state == bs_multiplex && sub_state_type::crat(mSubState)->idle);
    }
    // Return true if are added to the engine.
    bool active(AIEngine const* engine) const { return multiplex_state_type::crat(mState)->current_engine == engine; }
    bool aborted(void) const { return sub_state_type::crat(mSubState)->aborted; }

    // Use some safebool idiom (http://www.artima.com/cppsource/safebool.html) rather than operator bool.
    typedef state_type AIStatefulTask::* bool_type;
    // Return true if the task successfully finished.
    operator bool_type() const
    {
      sub_state_type::crat sub_state_r(mSubState);
      return (sub_state_r->finished && !sub_state_r->aborted) ? &AIStatefulTask::mNewParentState : 0;
    }

    // Return stringified state, for debugging purposes.
    char const* state_str(base_state_type state);
#ifdef CWDEBUG
    char const* event_str(event_type event);
#endif

    void add(duration_type delta) { mDuration += delta; }
    duration_type getDuration(void) const { return mDuration; }

  protected:
    virtual void initialize_impl(void) = 0;
    virtual void multiplex_impl(state_type run_state) = 0;
    virtual void abort_impl(void) { }
    virtual void finish_impl(void) { }
    virtual char const* state_str_impl(state_type run_state) const = 0;
    virtual void force_killed(void);            // Called from AIEngine::flush().

  private:
    void reset(void);                           // Called from run() to (re)initialize a (re)start.
    void multiplex(event_type event);           // Called from AIEngine to step through the states (and from reset() to kick start the task).
    state_type begin_loop(base_state_type base_state);  // Called from multiplex() at the start of a loop.
    void callback(void);                        // Called when the task finished.
    bool sleep(clock_type::time_point current_time)   // Count frames if necessary and return true when the task is still sleeping.
    {
      if (mSleep == 0)
        return false;
      else if (mSleep < 0)
        ++mSleep;
      else if (mSleep <= current_time.time_since_epoch().count())
        mSleep = 0;
      return mSleep != 0;
    }

    friend class AIEngine;                      // Calls multiplex() and force_killed().
};

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
extern channel_ct statefultask;
NAMESPACE_DEBUG_CHANNELS_END
#endif

// This can be used in state_str_impl.
#define AI_CASE_RETURN(x) do { case x: return #x; } while(0)
