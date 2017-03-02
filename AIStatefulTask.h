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

#ifdef CW_DEBUG_MONTECARLO

#define MonteCarloProbeFileState(task_state, ...) do { probe(__FILE__, __LINE__, task_state, __VA_ARGS__); } while(0)
#else  // CW_DEBUG_MONTECARLO
#define MonteCarloProbeFileState(task_state, ...) do { } while(0)
#endif // CW_DEBUG_MONTECARLO

#include "utils/AIRefCount.h"
#include "threadsafe/aithreadsafe.h"
#include "threadsafe/AIMutex.h"
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
      multiplex_state_st() : base_state(bs_reset), current_engine(nullptr) { }
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
#ifdef CW_DEBUG_MONTECARLO
      // In order to have reproducible results (otherwise these are initially uninitialized).
      sub_state_st() : run_state(-1), advance_state(-1), blocked(nullptr), reset(false), need_run(false), idle(false), skip_idle(false), aborted(false), finished(false) { }
#endif
    };

#ifdef CW_DEBUG_MONTECARLO
  public:
    struct task_state_st : public multiplex_state_st, public sub_state_st {
      bool blocked;                     // True iff sub_state_st::blocked is non-null.
      char const* base_state_str;       // Human readable version of multiplex_state_st::base_state.
      char const* run_state_str;        // Human readable version of sub_state_st::run_state.
      char const* advance_state_str;    // Human readable version of sub_state_st::advance_state.
      bool reset_m_state_locked_at_end_of_probe;
      bool reset_m_sub_state_locked_at_end_of_probe;
      int inside_probe_impl;            // Count of number of recursions into probe_impl().

      bool equivalent(task_state_st const& task_state) const
      {
        return base_state == task_state.base_state &&
               run_state == task_state.run_state &&
               advance_state == task_state.advance_state &&
               blocked == task_state.blocked &&
               reset == task_state.reset &&
               idle == task_state.idle &&
               skip_idle == task_state.skip_idle &&
               aborted == task_state.aborted &&
               finished == task_state.finished;
      }
    };
#endif

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

#ifdef DEBUG
    // Debug stuff.
    std::thread::id mThreadId;          // The thread currently running multiplex() (or std::thread::id() when none).
    base_state_type mDebugLastState;    // The previous state that multiplex() had a normal run with.
    bool mDebugShouldRun;               // Set if we found evidence that we should indeed call multiplex_impl().
    bool mDebugAborted;                 // True when abort() was called.
    bool mDebugContPending;             // True while cont() was called but not handled yet.
    bool mDebugSetStatePending;         // True while set_state() was called by not handled yet.
    bool mDebugAdvanceStatePending;     // True while advance_state() was called by not handled yet.
    bool mDebugRefCalled;               // True when ref() is called (or will be called within the critial area of mMultiplexMutex).
#endif
#ifdef CWDEBUG
  protected:
    bool mSMDebug;                      // Print debug output only when true.
#endif
#ifdef CW_DEBUG_MONTECARLO
  protected:
    int m_inside_probe_impl;
    mutable multiplex_state_type::crat const* m_state_locked;
    mutable sub_state_type::crat const* m_sub_state_locked;
#endif
  private:
    duration_type mDuration;            // Total time spent running in the main thread.

  public:
    AIStatefulTask(DEBUG_ONLY(bool debug)) : mCallback(nullptr), mDefaultEngine(nullptr), mYieldEngine(nullptr),
#ifdef DEBUG
    mDebugLastState(bs_killed), mDebugShouldRun(false), mDebugAborted(false), mDebugContPending(false),
    mDebugSetStatePending(false), mDebugAdvanceStatePending(false), mDebugRefCalled(false),
#endif
#ifdef CWDEBUG
    mSMDebug(debug),
#endif
#ifdef CW_DEBUG_MONTECARLO
    m_inside_probe_impl(0), m_state_locked(nullptr), m_sub_state_locked(nullptr),
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
    void run(AIStatefulTask* parent, state_type new_parent_state, bool abort_parent = true, bool on_abort_signal_parent = true, AIEngine* default_engine = &gMainThreadEngine);
    void run(callback_type::signal_type::slot_type const& slot, AIEngine* default_engine = &gMainThreadEngine);
    void run(AIEngine* default_engine = nullptr) { run(nullptr, 0, false, true, default_engine); }

    // This function may only be called from the call back function (and cancels a call to run() from finish_impl()).
    void kill();

  protected:
    // This function can be called from initialize_impl() and multiplex_impl() (both called from within multiplex()).
    void set_state(state_type new_state);       // Run this state the NEXT loop.
    // These functions can only be called from within multiplex_impl().
    void idle();                                // Go idle unless cont() or advance_state() were called since the start of the current loop, or until they are called.
    void wait(AIConditionBase& condition);      // The same as idle(), but wake up when AICondition<T>::signal() is called.
    void finish();                              // Mark that the task finished and schedule the call back.
    void yield();                               // Yield to give CPU to other tasks, but do not go idle.
    void yield(AIEngine* engine);               // Yield to give CPU to other tasks, but do not go idle. Continue running from engine 'engine'.
    void yield_frame(unsigned int frames);      // Run from the main-thread engine after at least 'frames' frames have passed.
    void yield_ms(unsigned int ms);             // Run from the main-thread engine after roughly 'ms' miliseconds have passed.
    bool yield_if_not(AIEngine* engine);        // Do not really yield, unless the current engine is not 'engine'. Returns true if it switched engine.

  public:
    // This function can be called from multiplex_imp(), but also by a child task and
    // therefore by any thread. The child task should use an boost::intrusive_ptr<AIStatefulTask>
    // to access this task.
    void abort();                               // Abort the task (unsuccessful finish).

    // These are the only three functions that can be called by any thread at any moment.
    // Those threads should use an boost::intrusive_ptr<AIStatefulTask> to access this task.
    void cont();                                // Guarantee at least one full run of multiplex() after this function is called. Cancels the last call to idle().
    void advance_state(state_type new_state);   // Guarantee at least one full run of multiplex() after this function is called
    // iff new_state is larger than the last state that was processed.
    bool signalled();                           // Call cont() iff this task is still blocked after a call to wait(). Returns false if it already unblocked.

  public:
    // Accessors.

    // Return true if the derived class is running (also when we are idle).
    bool running() const { return multiplex_state_type::crat(mState)->base_state == bs_multiplex; }
    // Return true if the derived class is running and idle.
    bool waiting() const
    {
      multiplex_state_type::crat state_r(mState);
      return state_r->base_state == bs_multiplex && sub_state_type::crat(mSubState)->idle;
    }
    // Return true if the derived class is running and idle or already being aborted.
    bool waiting_or_aborting() const
    {
      multiplex_state_type::crat state_r(mState);
      return state_r->base_state == bs_abort || ( state_r->base_state == bs_multiplex && sub_state_type::crat(mSubState)->idle);
    }
    // Return true if we are added to the current engine.
    bool active(AIEngine const* engine) const { return multiplex_state_type::crat(mState)->current_engine == engine; }
    bool aborted() const { return sub_state_type::crat(mSubState)->aborted; }

    // Use some safebool idiom (http://www.artima.com/cppsource/safebool.html) rather than operator bool.
    typedef state_type AIStatefulTask::* bool_type;
    // Return true if the task successfully finished.
    operator bool_type() const
    {
      sub_state_type::crat sub_state_r(mSubState);
      return (sub_state_r->finished && !sub_state_r->aborted) ? &AIStatefulTask::mNewParentState : 0;
    }
    // Return true if this thread is executing this task right now (aka, we're inside multiplex() somewhere).
    bool executing() const { return mMultiplexMutex.self_locked(); }

#ifdef CW_DEBUG_MONTECARLO
    task_state_st do_copy_state(multiplex_state_type::crat const& state_r, bool reset_m_state_locked_at_end_of_probe,
                                sub_state_type::crat const& sub_state_r, bool reset_m_sub_state_locked_at_end_of_probe) const;
    task_state_st copy_state(multiplex_state_type::crat const& state_r, sub_state_type::crat const& sub_state_r) const
    {
      ASSERT(!m_sub_state_locked);
      ASSERT(!m_state_locked || m_state_locked == &state_r);
      bool m_state_locked_was_set = m_state_locked;
      m_state_locked = &state_r;
      m_sub_state_locked = &sub_state_r;
      return do_copy_state(state_r, !m_state_locked_was_set, sub_state_r, true);
    }
    task_state_st copy_state(multiplex_state_type::crat const& state_r) const
    {
      ASSERT(!m_state_locked && !m_sub_state_locked);
      m_state_locked = &state_r;
      return do_copy_state(state_r, true, sub_state_type::crat(mSubState), true);
    }
    task_state_st copy_state() const
    {
      if (m_sub_state_locked)
        return do_copy_state(*m_state_locked, false, *m_sub_state_locked, false);
      else if (m_state_locked)
        return do_copy_state(*m_state_locked, false, sub_state_type::crat(mSubState), true);
      else
      {
        // Make sure mState is locked first.
        multiplex_state_type::crat state_r(mState);
        return do_copy_state(state_r, true, sub_state_type::crat(mSubState), true);
      }
    }
    void probe(char const* file, int line, task_state_st state, char const* description,
        int s1 = -1, char const* s1_str = nullptr, int s2 = -1, char const* s2_str = nullptr, int s3 = -1, char const* s3_str = nullptr)
        {
          ++m_inside_probe_impl;
          probe_impl(file, line, state, description, s1, s1_str, s2, s2_str, s3, s3_str);
          --m_inside_probe_impl;
          if (state.reset_m_state_locked_at_end_of_probe)
            m_state_locked = nullptr;
          if (state.reset_m_sub_state_locked_at_end_of_probe)
            m_sub_state_locked = nullptr;
        }
#endif

    // Return stringified state, for debugging purposes.
    static char const* state_str(base_state_type state);
#ifdef CWDEBUG
    static char const* event_str(event_type event);
#endif

    void add(duration_type delta) { mDuration += delta; }
    duration_type getDuration() const { return mDuration; }

  protected:
    virtual void initialize_impl() = 0;
    virtual void multiplex_impl(state_type run_state) = 0;
    virtual void abort_impl() { }
    virtual void finish_impl() { }
    virtual char const* state_str_impl(state_type run_state) const = 0;
    virtual void force_killed();                // Called from AIEngine::flush().
#ifdef CW_DEBUG_MONTECARLO
    virtual void probe_impl(char const* file, int line, task_state_st state, char const* description, int s1, char const* s1_str, int s2, char const* s2_str, int s3, char const* s3_str) = 0;
#endif

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
