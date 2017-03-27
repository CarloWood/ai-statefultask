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
#include "threadsafe/AIMutex.h"
#include "debug.h"
#include <list>
#include <chrono>
#include <functional>
#include <boost/signals2.hpp>

class AICondition;
class AIEngine;

extern AIEngine gMainThreadEngine;
extern AIEngine gAuxiliaryThreadEngine;

typedef std::function<bool()> AIWaitConditionFunc;

class AIStatefulTask : public AIRefCount
{
  public:
    typedef uint32_t state_type;        //!< The type of run_state.
    typedef uint32_t condition_type;    //!< The type of the busy, skip_wait and idle bit masks.

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
    enum on_abort_st { abort_parent, signal_parent, do_nothing };

  protected:
    struct multiplex_state_st {
      base_state_type base_state;
      AIEngine* current_engine;         // Current engine.
      AIWaitConditionFunc wait_condition;
      condition_type conditions;
      multiplex_state_st() : base_state(bs_reset), current_engine(nullptr), wait_condition(nullptr) { }
    };
    struct sub_state_st {
      condition_type busy;              //!< Each bit represents being not-idle for that condition-bit: wait(condition_bit) was never called or signal(condition_bit) was called last.
      condition_type skip_wait;         //!< Each bit represents having been signalled ahead of the call to wait(condition_bit) for that condition-bit: signal(condition_bit) was called while already busy.
      condition_type idle;              //!< A the idle state at the end of the last call to wait(conditions) (~busy & conditions).
      state_type run_state;
      bool reset;
      bool need_run;
      bool wait_called;
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
    AIMutex mMultiplexMutex;
    // Mutex that is locked while calling *_impl() functions and the call back.
    std::recursive_mutex mRunMutex;

    typedef std::chrono::steady_clock clock_type;
    typedef clock_type::duration duration_type;

    clock_type::rep mSleep;   //!< Non-zero while the task is sleeping. Negative means frames, positive means clock periods.

    // Callback facilities.
    // From within an other stateful task:
    boost::intrusive_ptr<AIStatefulTask> mParent;       // The parent object that started this task, or nullptr if there isn't any.
    condition_type mParentCondition;                    // The condition (bit) that the parent should be signalled with upon a successful finish.
    on_abort_st mOnAbort;                               // What to do with the parent (if any) when aborted.
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

#ifdef DEBUG
    // Debug stuff.
    std::thread::id mThreadId;          // The thread currently running multiplex() (or std::thread::id() when none).
    base_state_type mDebugLastState;    // The previous state that multiplex() had a normal run with.
    bool mDebugShouldRun;               // Set if we found evidence that we should indeed call multiplex_impl().
    bool mDebugAborted;                 // True when abort() was called.
    bool mDebugSignalPending;           // True while wait() was called but didn't get idle because of a pending call to signal() that wasn't handled yet.
    bool mDebugSetStatePending;         // True while set_state() was called by not handled yet.
    bool mDebugRefCalled;               // True when ref() is called (or will be called within the critial area of mMultiplexMutex).
#endif
#ifdef CWDEBUG
  protected:
    bool mSMDebug;                      // Print debug output only when true.
#endif
  private:
    duration_type mDuration;            // Total time spent running in the main thread.

  public:
    AIStatefulTask(DEBUG_ONLY(bool debug)) : mCallback(nullptr), mDefaultEngine(nullptr), mYieldEngine(nullptr),
#ifdef DEBUG
    mDebugLastState(bs_killed), mDebugShouldRun(false), mDebugAborted(false), mDebugSignalPending(false),
    mDebugSetStatePending(false), mDebugRefCalled(false),
#endif
#ifdef CWDEBUG
    mSMDebug(debug),
#endif
    mDuration(duration_type::zero()) { }

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
    void run(callback_type::signal_type::slot_type const& slot, AIEngine* default_engine = &gMainThreadEngine);
    void run(AIStatefulTask* parent, condition_type condition, on_abort_st on_abort = abort_parent, AIEngine* default_engine = &gMainThreadEngine);
    void run(AIEngine* default_engine = nullptr) { run(nullptr, 0, do_nothing, default_engine); }

    // This function may only be called from the call back function (and cancels a call to run() from finish_impl()).
    void kill();

  protected:
    // This function can be called from initialize_impl() and multiplex_impl() (both called from within multiplex()).
    void set_state(state_type new_state);       // Run this state the NEXT loop.
    // These functions can only be called from within multiplex_impl().
    void wait(condition_type conditions);       // Go idle if non of the bits of conditions were signalled twice or more since the last call to wait(that_bit).
                                                // The task will continue whenever signal(condition) is called where conditions & condition != 0.
    void wait(AIWaitConditionFunc const& wait_condition, condition_type conditions);   // Block until the wait_condition returns true.
                                                // Whenever something changed that might cause wait_condition to return true, signal(condition) must be called.
    void wait(AIWaitConditionFunc const& wait_condition, condition_type conditions, state_type new_state) { set_state(new_state); wait(wait_condition, conditions); }
    void finish();                              // Mark that the task finished and schedule the call back.
    void yield();                               // Yield to give CPU to other tasks, but do not block.
    void yield(AIEngine* engine);               // Yield to give CPU to other tasks, but do not block. Continue running from engine 'engine'.
    void yield_frame(unsigned int frames);      // Run from the main-thread engine after at least 'frames' frames have passed.
    void yield_ms(unsigned int ms);             // Run from the main-thread engine after roughly 'ms' miliseconds have passed.
    bool yield_if_not(AIEngine* engine);        // Do not really yield, unless the current engine is not 'engine'. Returns true if it switched engine.

  public:
    // This function can be called from multiplex_imp(), but also by a child task and
    // therefore by any thread. The child task should use an boost::intrusive_ptr<AIStatefulTask>
    // to access this task.
    void abort();                               // Abort the task (unsuccessful finish).

    // This is the only function that can be called by any thread at any moment.
    // Those threads should use an boost::intrusive_ptr<AIStatefulTask> to access this task.
    bool signal(condition_type condition);      // Guarantee at least one full run of multiplex() iff this task is still blocked since
                                                // the last call to wait(conditions) where (conditions & condition) != 0.
                                                // Returns false if it already unblocked or is waiting on (a) different condition(s) now.

  public:
    // Accessors.

    // Return true if the derived class is running (also when we are blocked).
    bool running() const { return multiplex_state_type::crat(mState)->base_state == bs_multiplex; }

    // Return true if the derived class is running and idle.
    bool waiting() const;

    // Return true if the derived class is running and idle or already being aborted.
    bool waiting_or_aborting() const;

    // Return true if we are added to the current engine.
    bool active(AIEngine const* engine) const { return multiplex_state_type::crat(mState)->current_engine == engine; }

    // Use some safebool idiom (http://www.artima.com/cppsource/safebool.html) rather than operator bool.
    typedef on_abort_st AIStatefulTask::* bool_type;
    // Return true if the task finished.
    // If this function returns false then the callback (or call to abort() on the parent) is guaranteed to still going to happen.
    // If this function returns true then the callback might have happened or might still going to happen.
    // Call aborted() to check if the task finished successfully if this function returns true (or just call that in the callback).
    operator bool_type() const
    {
      sub_state_type::crat sub_state_r(mSubState);
      return sub_state_r->finished ? &AIStatefulTask::mOnAbort : 0;
    }

    // Return true if this task was aborted. This value is guaranteed to be valid (only) after the task finished.
    bool aborted() const { return sub_state_type::crat(mSubState)->aborted; }

    // Return true if this thread is executing this task right now (aka, we're inside multiplex() somewhere).
    bool executing() const { return mMultiplexMutex.self_locked(); }

    // Return stringified state, for debugging purposes.
    static char const* state_str(base_state_type state);
#ifdef CWDEBUG
    static char const* event_str(event_type event);
#endif

    void add(duration_type delta) { mDuration += delta; }
    duration_type getDuration() const { return mDuration; }

  protected:
    virtual char const* state_str_impl(state_type run_state) const = 0;
    virtual void initialize_impl() = 0;
    virtual void multiplex_impl(state_type run_state) = 0;
    virtual void abort_impl();
    virtual void finish_impl();
    virtual void force_killed();                // Called from AIEngine::flush().

  private:
    void reset();                               // Called from run() to (re)initialize a (re)start.
    void multiplex(event_type event, AIEngine* engine = nullptr); // Called to step through the states. If event == normal_run then engine is the engine this was called from.
    state_type begin_loop(base_state_type base_state);  // Called from multiplex() at the start of a loop.
    void callback();                            // Called when the task finished.
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
