/**
 * ai-statefultask -- Asynchronous, Stateful Task Scheduler library.
 *
 * @file
 * @brief Declaration of base class AIStatefulTask.
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

#ifndef AISTATEFULTASK_H
#define AISTATEFULTASK_H

#include "threadsafe/aithreadsafe.h"
#include "threadsafe/AIMutex.h"
#include "threadpool/AIQueueHandle.h"
#include "utils/AIRefCount.h"
#include "utils/macros.h"
#include "debug.h"
#include <list>
#include <chrono>
#include <functional>

class AIEngine;
class AIStatefulTaskMutex;

/// The type of the functor that must be passed as first parameter to AIStatefulTask::wait_until.
using AIWaitConditionFunc = std::function<bool()>;

/**
 * Base class for task objects.
 *
 * Derive a new task from this base class.
 * The derived class must have a protected destructor that uses the @c{override} keyword.
 *
 * Furthermore a derived class must define,
 * <table class="implement_table">
 * <tr><td class="item">@link example_task direct_base_type @endlink<td>The immediate base class of the derived class.
 * <tr><td class="item">@link example_task foo_state_type @endlink<td>An <code>enum</code> with the (additional) states of the task, where the first state must have the value <code>direct_base_type::state_end</code>.
 * <tr><td class="item">@link example_task state_end @endlink<td>A <code>static state_type constexpr</code> with a value one larger than its largest state.
 * <tr><td class="item">@link Example::state_str_impl state_str_impl @endlink<td>A member function that returns a <code>char const*</code> to a human readable string for each of its states (for debug purposes).
 * <tr><td class="item">@link Example::multiplex_impl multiplex_impl @endlink<td>The core function with a <code>switch</code> on the current state to be run.
 * </table>
 *
 * And optionally override zero or more of the following virtual functions:
 * <table class="implement_table">
 * <tr><td class="item">@link Example::initialize_impl initialize_impl@endlink<td>Member function that is called once upon creation.
 * <tr><td class="item">@link Example::abort_impl abort_impl@endlink<td>Member function that is called when the task is aborted.
 * <tr><td class="item">@link Example::finish_impl finish_impl@endlink<td>Member function that is called when the task finished.
 * <tr><td class="item">@link Example::force_killed force_killed@endlink<td>Member function that is called when the task is killed.
 * </table>
 *
 * @sa Example
 */
class AIStatefulTask : public AIRefCount
{
 public:
  using state_type = uint32_t;        ///< The type of run_state.
  using condition_type = uint32_t;    ///< The type of the busy, skip_wait and idle bit masks.

 private:
  /// The type of event that causes <code>multiplex(event_type event)</code> to be called.
  enum event_type {
    initial_run,              ///< The user called @c run, directly after creating a task.
    schedule_run,             ///< The user called signal(condition_type) with a condition that the task was waiting for.
    normal_run,               ///< Called from AIEngine::mainloop for tasks in the engines queue.
    insert_abort              ///< Called from abort() when that is called on a waiting task.
  };

 protected:
  /// The type of @c mState.
  enum base_state_type {
    bs_reset,                 ///< Idle state before @c run is called. Reference count is zero (except for a possible external <code>boost::intrusive_ptr</code>).
    bs_initialize,            ///< State after @c run and before/during @c initialize_impl.
    bs_multiplex,             ///< State after @c initialize_impl and before @c finish() or @c abort().
    bs_abort,                 ///< State after @c abort() <em>and</em> leaving @c inside multiplex_impl (if there), and before @c abort_impl().
    bs_finish,                ///< State after @c finish() (assuming @c abort() isn't called) <em>and</em> leaving @c multiplex_impl (if there), or after @c abort_impl, and before @c finish_impl().
    bs_callback,              ///< State after @c finish_impl() and before the call back.
    bs_killed                 ///< State after the call back, or when aborted before being initialized.
  };

 public:
  /// The next state value to use for derived classes.
  static state_type constexpr state_end = bs_killed + 1;
  /// What to do when a child task is aborted.
  enum on_abort_st {
    abort_parent,             ///< Call abort() on the parent.
    signal_parent,            ///< Call signal(condition_type) on the parent anyway.
    do_nothing                ///< Abort without notifying the parent task.
  };

  /**
   * Describes if, how and where to run a task.
   *
   * By constructing a Handler from Handler::idle, it describes that a task is <b>idle</b>,
   * <b>or</b> that Handler is <b>not to be used</b> when other alternatives exist.
   *
   * By constructing a Handler from Handler::immediate, it describes that a task
   * should run in the thread that calls run() or the thread that wakes up a task
   * by calling signal(). Hence, this handler causes a task to run to until
   * the first time it goes idle, or calls @c yield, before returning from @c run / @c signal respectively.
   *
   * When a Handler is constructed from an AIEngine pointer, then it describes that
   * a task should run in that engine.
   *
   * Finally, a Handler can be constructed from AIQueueHandle, describing that a
   * task should run in the thread pool by adding it to the PriorityQueue of that
   * handle.
   */
  struct Handler
  {
    /// The type of \ref m_type.
    enum type_t {
      idle_h,           ///< An idle Handler.
      immediate_h,      ///< An immediate Handler.
      engine_h,         ///< An engine Handler.
      thread_pool_h     ///< A thread pool Handler.
    };
    /// A typed use by the constuctor Handler(special_t).
    enum special_t {
      idle = idle_h,            ///< Construct an idle Handler.
      immediate = immediate_h   ///< Construct an immediate Handler.
    };
    /// Contains extra data that depends on the type of the Handler.
    union Handle {
      AIEngine* engine;                 ///< The actual engine when this is an engine Handler.
      AIQueueHandle queue_handle;       ///< The actual thread pool queue when this is a thread pool Handler.

      /// Construct an uninitialized Handle.
      Handle() { }
      /// Construct a Handle from an AIEngine pointer.
      Handle(AIEngine* engine_) : engine(engine_) { }
      /// Construct a Handle from a thread pool handle.
      Handle(AIQueueHandle queue_handle_) : queue_handle(queue_handle_) { }
    };
    Handle m_handle;    ///< Extra data that depends on m_type.
    type_t m_type;      ///< The type of this Handler.

    /// Construct a special Handler.
    Handler(special_t special) : m_type((type_t)special) { }
    /// Construct a Handler from an AIEngine pointer.
    Handler(AIEngine* engine) : m_handle(engine), m_type(engine_h) { ASSERT(engine); }
    /// Construct a Handler from an AIQueueHandle.
    Handler(AIQueueHandle queue_handle) : m_handle(queue_handle), m_type(thread_pool_h) { }
    /// Return true if this is an engine Handler.
    bool is_engine() const { return m_type == engine_h; }
    /// Return true if this is an @link immediate_h immediate @endlink Handler.
    bool is_immediate() const { return m_type == immediate_h; }
    /// Return the AIQueueHandle to use (only call when appropriate).
    AIQueueHandle get_queue_handle() const { return m_type == thread_pool_h ? m_handle.queue_handle : AIQueueHandle((std::size_t)0); }
    /// Return true when not idle / unused.
    operator bool() const { return m_type != idle_h; }        // Return true when this handler can be used to actually run in.

    // Gcc complains because what is defined depends on the value of m_type.
    PRAGMA_DIAGNOSTIC_PUSH_IGNORE_maybe_uninitialized
    /// Return true when equivalent to @a handler.
    bool operator==(Handler handler) const {
        return m_type == handler.m_type &&
            (m_type != engine_h || m_handle.engine == handler.m_handle.engine) &&
            (m_type != thread_pool_h || m_handle.queue_handle == handler.m_handle.queue_handle); }
    PRAGMA_DIAGNOSTIC_POP

    friend std::ostream& operator<<(std::ostream& os, Handler const& handler);
  };

 protected:
#ifndef DOXYGEN
  struct multiplex_state_st {
    base_state_type base_state;
    Handler current_handler;            // Current handler.
    AIWaitConditionFunc wait_condition;
    condition_type conditions;
    multiplex_state_st() : base_state(bs_reset), current_handler(Handler::idle), wait_condition(nullptr) { }
  };

  struct sub_state_st {
    condition_type busy;              ///< Each bit represents being not-idle for that condition-bit: wait(condition_bit) was never called or signal(condition_bit) was called last.
    condition_type skip_wait;         ///< Each bit represents having been signalled ahead of the call to wait(condition_bit) for that condition-bit: signal(condition_bit) was called while already busy.
    condition_type idle;              ///< A the idle state at the end of the last call to wait(conditions) (~busy & conditions).
    state_type run_state;
    bool reset;
    bool need_run;
    bool wait_called;
    bool aborted;
    bool finished;
  };

 private:
  // Base state.
  using multiplex_state_type = aithreadsafe::Wrapper<multiplex_state_st, aithreadsafe::policy::Primitive<std::mutex>>;
  multiplex_state_type mState;

 protected:
  // Sub state.
  using sub_state_type = aithreadsafe::Wrapper<sub_state_st, aithreadsafe::policy::Primitive<std::mutex>>;
  sub_state_type mSubState;
#endif // DOXYGEN

 private:
  // Mutex protecting everything below and making sure only one thread runs the task at a time.
  AIMutex mMultiplexMutex;
  // Mutex that is locked while calling *_impl() functions and the call back.
  std::recursive_mutex mRunMutex;

  using clock_type = std::chrono::steady_clock;
  using duration_type = clock_type::duration;

  clock_type::rep mSleep;   ///< Non-zero while the task is sleeping. Negative means frames, positive means clock periods.

  // Callback facilities.
  // From within an other stateful task:
  boost::intrusive_ptr<AIStatefulTask> mParent;       // The parent object that started this task, or nullptr if there isn't any.
  condition_type mParentCondition;                    // The condition (bit) that the parent should be signalled with upon a successful finish.
  on_abort_st mOnAbort;                               // What to do with the parent (if any) when aborted.
  // From outside a stateful task:
  std::function<void (bool)>mCallback;                // Pointer to signal/connection, or nullptr when not connected.

  // Engine stuff.
  Handler mDefaultHandler;            // Default engine or queue.
  Handler mTargetHandler;             // Requested engine by a call to yield.
  bool mYield;                        // True when any yield function was called, except for yield_if_not when the passed engine already matched.

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

#if defined(CWDEBUG) && !defined(DOXYGEN)
 protected:
  bool mSMDebug;                      // Print debug output only when true.
#endif

 private:
  duration_type mDuration;            // Total time spent running in the main thread.

 public:
  /**
   * Constructor of base class AIStatefulTask.
   *
   * The @a debug parameter only exists when CWDEBUG is defined.
   *
   * The following parameter is only available in debug mode.
   * @param debug Write debug output for this task to dc::statefultask.
   */
  AIStatefulTask(CWDEBUG_ONLY(bool debug)) : mDefaultHandler(Handler::idle), mTargetHandler(Handler::idle), mYield(false),
#ifdef DEBUG
  mDebugLastState(bs_killed), mDebugShouldRun(false), mDebugAborted(false), mDebugSignalPending(false),
  mDebugSetStatePending(false), mDebugRefCalled(false),
#endif
#ifdef CWDEBUG
  mSMDebug(debug),
#endif
  mDuration(duration_type::zero()) { }

 protected:
  /// Destructor.
  virtual ~AIStatefulTask()
  {
#ifdef DEBUG
    base_state_type state = multiplex_state_type::rat(mState)->base_state;
    ASSERT(state == bs_killed || state == bs_reset);
#endif
  }

 public:
  /**
   * @addtogroup group_run Running tasks
   * @{
   * Start a new task or restart an existing task that just finished.
   * These functions may be called directly after creation, or from within @link Example::finish_impl finish_impl @endlink, or from the call back function.
   *
   * @sa page_default_engine
   */

  /**
   * (Re)run a task with default handler @a default_handler, requesting
   * a call back to a function <code>void cb_function(bool success)</code>.
   * The parameter @c success will be @c true when the task finished successfully, or @c false when it was aborted.
   *
   * @param default_handler The way that this task should be handled by default.
   * @param cb_function The call back function. This function will be called with a single parameter with type @c bool.
   */
  void run(Handler default_handler, std::function<void (bool)> cb_function);

  /// The same as above but use the @link AIStatefulTask::Handler::immediate_h immediate @endlink Handler.
  void run(std::function<void (bool)> cb_function) { run(Handler::immediate, cb_function); }

  /**
   * (Re)run a task with default handler @a default_handler, requesting
   * to signal the parent on condition @a condition when successfully finished.
   *
   * Upon an abort the parent can either still be signalled, also aborted or be left in limbo (do nothing).
   *
   * @param default_handler The way that this task should be handled by default.
   * @param parent The parent task.
   * @param condition The condition of the parent that will be signalled.
   * @param on_abort What to do with the parent when this task is aborted.
   */
  void run(Handler default_handler, AIStatefulTask* parent, condition_type condition, on_abort_st on_abort = abort_parent);

  /**
   * The same as above but use the @link AIStatefulTask::Handler::immediate_h immediate @endlink Handler.
   */
  void run(AIStatefulTask* parent, condition_type condition, on_abort_st on_abort = abort_parent) { run(Handler::immediate, parent, condition, on_abort); }

  /**
   * Just run the bloody task (no call back).
   *
   * @param default_handler The default engine or thread pool queue that the task be added to.
   */
  void run(Handler default_handler = Handler::immediate) { run(default_handler, nullptr, 0, do_nothing); }

  ///@} // group_run

  /**
   * @addtogroup group_public Public control functions.
   * @{
   */
  /**
   * Terminate a task from the call back.
   *
   * This function may <em>only</em> be called from the call back function (and cancels a call to
   * @link group_run run@endlink from @link Example::finish_impl finish_impl@endlink).
   */
  void kill();
  ///@}

 protected:
  /**
   * @addtogroup group_protected Protected control functions.
   * @{
   * From within the @c multiplex_impl function of a running task, the following
   * member functions may be called to control the task.
   *
   * @sa group_public
   * @sa page_state_evolution
   */

  /**
   * Set the state to run, the next invocation of multiplex_impl.
   *
   * This function can be called from @c initialize_impl and @c multiplex_impl.
   *
   * A call to @c set_state has no immediate effect, except that the <em>next</em>
   * invokation of @c multiplex_impl will be with the state that was passed to
   * the (last) call to @c set_state.
   *
   * @param new_state The state to run the next invocation of @c multiplex_impl.
   *
   * @internal Both, initialize_impl and multiplex_impl are called from within AIStatefulTask::multiplex.
   */
  void set_state(state_type new_state);       // Run this state the NEXT loop.

  /**
   * @addtogroup group_wait Going idle and waiting for an event.
   *
   * For a more detailed usage description and overview please see the @link waiting main page@endlink.
   *
   * @{
   * These member functions can only be called from within @c multiplex_impl.
   */
  /**
   * Wait for @a condition.
   *
   * Go idle if non of the bits of @a conditions were signalled <em>twice</em> or more since the last call to <code>wait(</code>that_bit<code>)</code>.
   * The task will continue whenever <code>signal(condition)</code> is called where <code>conditions &amp; condition != 0</code>.
   *
   * @param conditions A bit mask of conditions to wait for.
   */
  void wait(condition_type conditions);

  /**
   * Block until the @a wait_condition returns true.
   *
   * Whenever something changed that might cause @a wait_condition to return @c true, <code>signal(condition)</code> must be called.
   * Calling <code>signal(condition)</code> more often is okay.
   *
   * @param wait_condition A <code>std::function&lt;bool()&gt;</code> that must return @c true.
   * @param conditions A bit mask of conditions to wait for.
   */
  void wait_until(AIWaitConditionFunc const& wait_condition, condition_type conditions);

  /**
   * Block until the @a wait_condition returns true.
   *
   * Whenever something changed that might cause @a wait_condition to return @c true, <code>signal(condition)</code> must be called.
   * Calling <code>signal(condition)</code> more often is okay.
   *
   * @param wait_condition A <code>std::function&lt;bool()&gt;</code> that must return @c true.
   * @param conditions A bit mask of conditions to wait for.
   * @param new_state The new state to continue with once @a wait_condition returns @c true.
   */
  void wait_until(AIWaitConditionFunc const& wait_condition, condition_type conditions, state_type new_state)
  {
    set_state(new_state);
    wait_until(wait_condition, conditions);
  }

  ///@} // group_wait

  /**
   * Mark that the task finished and schedule the call back.
   *
   * A call to @c abort and @c finish will cause a task to not call
   * @c multiplex_impl again at all after leaving it (except when
   * @c finish_impl calls @c run again, in which case the task is
   * restarted from the beginning).
   */
  void finish();

  /**
   * Return true when stateful_task_mutex is self locked.
   */
  inline bool is_self_locked(AIStatefulTaskMutex& stateful_task_mutex);

  /**
   * A call to yield*() has basically no effect on a task, except that its
   * execution is delayed a bit: this causes the task to return to the
   * AIEngine main loop code. That way other running tasks (in that engine)
   * will get the chance to execute before the current task is continued.
   *
   * Note that if a state calls @c yield*() without calling first @ref set_state
   * then this might <em>still</em> cause 100% cpu usage despite the call to @c yield*
   * if the task runs in an engine without @link AIEngine::setMaxDuration max_duration@endlink because the task
   * will rapidly execute again and then call @c yield* over and over:
   * an engine without max_duration keeps iterating over its tasks until all tasks finished.
   * Otherwise the engine will return from AIEngine::mainloop regardless after at least
   * @c AIEngine::sMaxDuration milliseconds have passed (which can be set by calling
   * @link AIEngine::setMaxDuration AIEngine::setMaxDuration(milliseconds)@endlink).
   *
   * @addtogroup group_yield Yield and engine control
   * @{
   */

  /**
   * Yield to give CPU to other tasks, but do not block.
   *
   * If a task runs potentially too long, it is a good idea to
   * regularly call this member function and break out of
   * @c multiplex_impl in order not to let other tasks in the
   * same engine starve.
   */
  void yield();

  /**
   * Continue running from @a engine.
   *
   * The task will keep running in this engine until @c target is called again.
   * Call <code>target(Handler::idle)</code> to return to running freely (with a default engine, etc, if one was given).
   *
   * @param handler The required engine or thread pool queue to run in, or Handler::idle to turn the target off.
   */
  void target(Handler handler);

  /**
   * The above two combined.
   *
   * @param handler The required engine or thread pool queue to run in.
   */
  void yield(Handler handler);

  /**
   * Switch to @a engine and sleep for @a frames frames.
   *
   * This function can only be used for an @a engine with a max_duration.
   * One frame means one entry into @c AIEngine::mainloop. So, for @a frames
   * entries of @c mainloop this task will not be run.
   *
   * @param engine The engine to sleep in. This must be an engine with a max_duration set.
   * @param frames The number frames to run before returning CPU to other tasks (if any).
   */
  void yield_frame(AIEngine* engine, unsigned int frames);

  /**
   * Switch to @a engine and sleep for @a ms milliseconds.
   *
   * This function can only be used for an engine with a max_duration.
   *
   * @param engine The engine to sleep in. This must be an engine with a max_duration set.
   * @param ms The number of miliseconds to run before returning CPU to other tasks (if any).
   */
  void yield_ms(AIEngine* engine, unsigned int ms);

  /**
   * Do not really yield, unless the current engine is not @a engine.
   *
   * @param handler The required engine or thread pool queue to run in.
   * @returns true if it switched engine.
   */
  bool yield_if_not(Handler handler);

  ///@} // group_yield
  ///@} // group_protected

  // Calls wait_until.
  friend class AIFriendOfStatefulTask;

 public:
  /**
   * @addtogroup group_public
   * @{
   */
  /**
   * Abort the task (unsuccessful finish).
   *
   * This function can be called from @c multiplex_imp, but also by a child task and therefore by any thread.
   * The child task should use a <code>boost::intrusive_ptr&lt;AIStatefulTask&gt;</code> to access this task.
   *
   * A call to @c abort and @c finish will cause a task to not call
   * @c multiplex_impl again at all after leaving it (except when
   * @c finish_impl calls @c run again, in which case the task is
   * restarted from the beginning).
   */
  void abort();

  /**
   * Wake up a waiting task.
   *
   * This is the only control function that can be called by any thread at any moment.
   *
   * Those threads should use a <code>boost::intrusive_ptr&lt;AIStatefulTask&gt;</code> to access this task.
   *
   * Guarantee at least one full run of @a multiplex iff this task is still blocked since
   * the last call to <code>wait(conditions)</code> where <code>(conditions & condition) != 0</code>.
   *
   * @param condition The condition that might have changed, or that the task is waiting for.
   * @returns false if it already unblocked or is waiting on (a) different condition(s) now.
   */
  bool signal(condition_type condition);

  ///@} // group_public

 public:
  // Accessors.

  /**
   * Return true if the derived class is running.
   *
   * The task was initialized (<em>after</em> @c initialize_impl) and did not
   * call @c finish() or @c abort() yet (<em>before</em> @c finish() or @c abort()).
   *
   * When a task enters @c multiplex_impl it is guaranteed to be running.
   */
  bool running() const { return multiplex_state_type::crat(mState)->base_state == bs_multiplex; }

  /**
   * Return true if the derived class is running and idle.
   *
   * Running and idle since the last call to @link group_wait wait@endlink;
   * if already having been woken up due to a call to @link group_wait signal(condition)@endlink
   * then we're still waiting until we actually re-entered @c begin_loop.
   */
  bool waiting() const;

  /**
   * Return true if the derived class is running and idle or already being aborted.
   *
   * A task reached the state aborting after returning from @c initialize_impl
   * or @c multiplex_impl that called @ref abort().
   * Or when @ref abort() is called from the outside while the task is idle (in which
   * case we are in the state aborting directly after the call to @ref abort()).
   *
   * @sa waiting
   */
  bool waiting_or_aborting() const;

  /**
   * Return true if we are added to the current engine.
   *
   * @param handler The handler that this task is supposed to run in.
   * @returns True if this task is actually added to @a handler.
   */
  bool active(Handler handler) const { return multiplex_state_type::crat(mState)->current_handler == handler; }

  /**
   * Test if the current Handler is immediate.
   *
   * @returns True if this task is currently running in an immediate handler.
   * @sa Handler::is_immediate
   */
  bool is_immediate() const { return multiplex_state_type::crat(mState)->current_handler.is_immediate(); }

  /// @cond Doxygen_Suppress
  // For debugging purposes mainly.
  bool default_is_immediate() const { return mDefaultHandler.is_immediate(); }
  /// @endcond

  /**
   * Return true if the task finished.
   *
   * If this function returns false then the callback (or call to abort() on the parent) is guaranteed still going to happen.
   * If this function returns true then the callback might have happened or might still going to happen.
   * Call aborted() to check if the task finished successfully if this function returns true (or just call that in the callback).
   */
  bool finished() const
  {
    sub_state_type::crat sub_state_r(mSubState);
    return sub_state_r->finished ? &AIStatefulTask::mOnAbort : 0;
  }

  /**
   * Return true if this task was aborted.
   *
   * This value is guaranteed to be valid (only) after the task finished.
   */
  bool aborted() const { return sub_state_type::crat(mSubState)->aborted; }

  /**
   * Return true if this thread is executing this task right now (aka, we're inside @c{multiplex} somewhere).
   */
  bool executing() const { return mMultiplexMutex.is_self_locked(); }

  /**
   * Return stringified state, for debugging purposes.
   *
   * @param state A base state.
   */
  static char const* state_str(base_state_type state);
#ifdef CWDEBUG
  /**
   * Return stringified event, for debugging purposes.
   *
   * @param event An event.
   */
  static char const* event_str(event_type event);
#endif

  /**
   * Return total time that this task has been running.
   *
   * May only be called from the main thread.
   */
  duration_type getDuration() const { return mDuration; }

 protected:
  /**
   * @{
   *
   * See @ref example_task for a description of the virtual functions.
   */
  /// Called to stringify a run state for debugging output. Must be overridden.
  virtual char const* state_str_impl(state_type run_state) const;
  /// Called for base state @ref bs_initialize.
  virtual void initialize_impl();
  /// Called for base state @ref bs_multiplex.
  virtual void multiplex_impl(state_type run_state) = 0;
  /// Called for base state @ref bs_abort.
  virtual void abort_impl();
  /// Called for base state @ref bs_finish.
  virtual void finish_impl();
  /// Called from AIEngine::flush().
  virtual void force_killed();
  ///@}

 private:
  void reset();                                       // Called from run() to (re)initialize a (re)start.

  /**
   * Called to step through the states.
   *
   * If event == normal_run then engine is the engine this was called from, unused otherwise (set to nullptr).
   */
  void multiplex(event_type event, Handler handler = Handler::idle);

  state_type begin_loop();                            // Called from multiplex() at the start of a loop.
  void callback();                                    // Called when the task finished.
  bool sleep(clock_type::time_point current_time)     // Count frames if necessary and return true when the task is still sleeping.
  {
    if (mSleep == 0)
      return false;
    else if (mSleep < 0)
      ++mSleep;
    else if (mSleep <= current_time.time_since_epoch().count())
      mSleep = 0;
    return mSleep != 0;
  }

  void add(duration_type delta) { mDuration += delta; }

  friend class AIEngine;      // Calls multiplex(), force_killed() and add().
};

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
extern channel_ct statefultask;
NAMESPACE_DEBUG_CHANNELS_END
#endif

/// Tasks defined by the library project are put into this namespace.
namespace task {

/**
 * Convenience function to create tasks.
 *
 * Typical usage,
 *
 * @code
 * class ATask : public AIStatefulTask {
 *   ...
 *  public:
 *   void init(...);
 * };
 *
 * class SomeClass
 * {
 *   boost::intrusive_ptr<ATask> m_task;
 *
 *  public:
 *   SomeClass() : m_task(task::create<ATask>(/\* constructor arguments of ATask *\/)) { }
 *
 *   void initial_run(...)
 *   {
 *     m_task->init(...);
 *     m_task->run(...);
 *   }
 * };
 * @endcode
 *
 * Or, for a one-shot task
 *
 * @code
 * auto task = task::create<ATask>(/\* constructor arguments of ATask *\/);
 * task->init(...);
 * task->run(...);
 * @endcode
 */
template<typename TaskType, typename... ARGS, typename = typename std::enable_if<std::is_base_of<AIStatefulTask, TaskType>::value>::type>
boost::intrusive_ptr<TaskType> create(ARGS&&... args)
{
#ifdef CWDEBUG
#if CWDEBUG_LOCATION
  LibcwDoutScopeBegin(LIBCWD_DEBUGCHANNELS, ::libcwd::libcw_do, dc::statefultask)
  LibcwDoutStream << "Entering task::create<" << libcwd::type_info_of<TaskType>().demangled_name();
  (LibcwDoutStream << ... << (", " + libcwd::type_info_of<ARGS>().demangled_name())) << ">(" << join(", ", args...) << ')';
  LibcwDoutScopeEnd;
  ::NAMESPACE_DEBUG::Indent indentation(2);
#else
  DoutEntering(dc::evio, "task::create<>(" << join(", ", args...) << ')')
#endif
#endif
  TaskType* task = new TaskType(std::forward<ARGS>(args)...);
  AllocTag2(task, "Created with task::create");
  Dout(dc::statefultask, "Returning task pointer " << (void*)task << " [" << static_cast<AIStatefulTask*>(task) << "].");
  return task;
}

} // namespace task

#include "AIStatefulTaskMutex.h"

bool AIStatefulTask::is_self_locked(AIStatefulTaskMutex& stateful_task_mutex)
{
  return stateful_task_mutex.is_self_locked(this);
}

#endif // AISTATEFULTASK_H
