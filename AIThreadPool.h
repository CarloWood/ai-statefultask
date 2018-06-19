/**
 * @file
 * @brief Thread pool implementation.
 *
 * @Copyright (C) 2017  Carlo Wood.
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
 *   29/04/2017
 *   - Initial version, written by Carlo Wood.
 */

#pragma once

#include "AIObjectQueue.h"
#include "AIQueueHandle.h"
#include "debug.h"
#include "signal_safe_printf.h"
#include "threadsafe/AIReadWriteMutex.h"
#include "threadsafe/AIReadWriteSpinLock.h"
#include "threadsafe/aithreadid.h"
#include "threadsafe/aithreadsafe.h"
#include <thread>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <semaphore.h>

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
extern channel_ct action;
extern channel_ct threadpool;
NAMESPACE_DEBUG_CHANNELS_END
#endif

/*!
 * @brief The thread pool class.
 *
 * Only one AIThreadPool may exist at a time; and can subsequently be
 * accessed by a call to the static function AIThreadPool::instance().
 *
 * However, an AIThreadPool is not a singleton: it doesn't have
 * private constructors and it may not be constructed before main().
 * Also, it has a public move constructor (although only the thread
 * that created it may move it).
 * It is allowed to create an AIThreadPool and after some usage
 * destruct it; and then create a new one (this is not recommended).
 *
 * The <em>recommended</em> usage is:
 *
 * @code
 * int main()
 * {
 *   Debug(NAMESPACE_DEBUG::init());
 *
 *   AIThreadPool thread_pool;
 *   AIQueueHandle handler = thread_pool.new_queue(capacity);
 * ...
 *   // Use thread_pool or AIThreadPool::instance()
 * }
 * @endcode
 *
 * In order to <em>use</em> the thread pool one has to create one
 * or more task queues by calling AIThreadPool::new_queue. The
 * handle returned by that function can subsequently be used
 * to access the underlaying queue and move a <code>std::function<bool()></code>
 * object into it.
 *
 * For example,
 *
 * @code
 * ... (see above)
 *   // Create a new queue with a capacity of 32 and default priority.
 *   AIQueueHandle queue_handle = thread_pool.new_queue(32);
 *
 *   {
 *     // Get read access to AIThreadPool::m_queues.
 *     auto queues_access = thread_pool.queues_read_access();
 *     // Get a reference to one of the queues in m_queues.
 *     auto& queue = thread_pool.get_queue(queues_access, queue_handle);
 *     bool queue_full;
 *     {
 *       // Get producer accesses to this queue.
 *       auto queue_access = queue.producer_access();
 *       int length = queue_access.length();
 *       queue_full = length == queue.capacity();
 *       if (!queue_full)
 *       {
 *         // Place a lambda in the queue.
 *         queue_access.move_in([](){ std::cout << "Hello pool!\n"; return false; });
 *       }
 *     } // Release producer accesses, so another thread can write to this queue again.
 *     // This function must be called every time move_in was called
 *     // on a queue that was returned by thread_pool.get_queue.
 *     if (!queue_full) // Was move_in called?
 *       queue.notify_one();
 *   } // Release read access to AIThreadPool::m_queues so another thread can use AIThreadPool::new_queue again.
 * @endcode
 *
 * It is necessary to keep <code>AIThreadPool::m_queues</code> read locked \htmlonly&mdash;\endhtmlonly
 * by not destroying the object returned by \ref queues_read_access \htmlonly&mdash;\endhtmlonly
 * for as long as the write lock on the queue exists.
 * I.e., in the above code, the lifetime of <code>queues_access</code>
 * must exceed the lifetime of <code>queue_access</code>.
 *
 * This is necessary because as soon as that read lock is released
 * some other thread can call AIThreadPool::new_queue causing a
 * resize of <code>AIThreadPool::m_queues</code>, the underlaying vector of
 * <code>AIObjectQueue<std::function<bool()>></code> objects,
 * possibly moving the AIObjectQueue objects in memory, invalidating the
 * returned reference to the queue.
 *
 * @sa AIObjectQueue
 * @sa @link AIPackagedTask< R(Args...)> AIPackagedTask@endlink
 */
class AIThreadPool
{
 private:
  struct Worker;
  using worker_function_t = void (*)(int const);
  using worker_container_t = std::vector<Worker>;
  using workers_t = aithreadsafe::Wrapper<worker_container_t, aithreadsafe::policy::ReadWrite<AIReadWriteMutex>>;

  class Action
  {
    struct Semaphore
    {
      sem_t m_semaphore;

      Semaphore()
      {
        sem_init(&m_semaphore, 0 , 0);
        Dout(dc::action, "Semaphore initialized with count = 0");
      }

      ~Semaphore()
      {
        sem_destroy(&m_semaphore);
      }

#ifdef CWDEBUG
      int get_count()
      {
        int count;
        sem_getvalue(&m_semaphore, &count);
        return count;
      }

      friend std::ostream& operator<<(std::ostream& os, Semaphore& semaphore)
      {
        return os << "semaphore count = " << semaphore.get_count();
      }
#endif
    };

    static Semaphore s_semaphore;       // Global semaphore to wake up all threads of the thread pool.
    std::atomic_int m_required;         // Counter for the number of actions required for this specific task.
#ifdef CWDEBUG
    std::string m_name;
#endif

   public:
    Action(DEBUG_ONLY(std::string name)) : m_required(0) COMMA_DEBUG_ONLY(m_name(name)) { }
    Action(Action&& DEBUG_ONLY(rvalue)) : m_required(0) COMMA_DEBUG_ONLY(m_name(rvalue.m_name)) { ASSERT(rvalue.m_required == 0); }

    void still_required()
    {
      DEBUG_ONLY(int val =) m_required.fetch_add(1);
      Dout(dc::action, m_name << " Action::still_required(): m_required " << val << " --> " << val + 1);
    }

    void required()
    {
      /*DEBUG_ONLY(int val =)*/ m_required.fetch_add(1);
      sem_post(&s_semaphore.m_semaphore);
      //signal_safe_printf("\n%s Action::required(): m_required %d --> %d; After calling sem_post, semaphore count = %d\n",
      //    m_name.c_str(), val, val + 1, s_semaphore.get_count());
    }

    static void wakeup()
    {
      sem_post(&s_semaphore.m_semaphore);
      Dout(dc::action, "Action::wakeup(): After calling sem_post, " << s_semaphore);
    }

    int available(int& duty)
    {
      DoutEntering(dc::action|continued_cf, m_name << " Action::available(" << duty << ") : ");
      int queued;
      while ((queued = m_required.load()) > 0)
      {
        if (!m_required.compare_exchange_weak(queued, queued - 1))
          continue;
        if (duty++)
        {
          int res;
          do
          {
            res = sem_trywait(&s_semaphore.m_semaphore);
          }
          while (AI_UNLIKELY(res == -1 && errno == EINTR));
#ifdef CWDEBUG
          if (res == -1)
          {
            // It is possible that the semaphore count was zero if the task
            // that we just claimed also woke up a non-idle thread. That
            // thread now will have to go around the loop doing nothing.
            ASSERT(errno == EAGAIN);
            Dout(dc::action|error_cf, "sem_trywait: count was already 0");
          }
          else
          {
            Dout(dc::action, "After calling sem_trywait, " << s_semaphore);
          }
#endif
        }
        Dout(dc::finish, "m_required " << queued << " --> " << queued - 1);
        return queued;
      }
      Dout(dc::finish, "no");
      return 0;
    }

    static void wait()
    {
      // If this is the last thread that goes to sleep, it might be the last debug output for a while.
      // Therefore flush all debug output here.
      DoutEntering(dc::action, "Action::wait()");
      Debug(libcw_do.get_ostream()->flush());
      // Loop until sem_wait return 0, because if it returns -1
      // due to a signal then we can't rely on it that that was
      // our timer signal (it could be any signal). Nor am I
      // sure that it is portable to rely upon sem_wait returning
      // at all when a signal handler was called.
      int res;
      do
      {
        res = sem_wait(&s_semaphore.m_semaphore);
        // Other errors never happen, do they?
        ASSERT(res == 0 || errno == EINTR);
      }
      while (res == -1);
      Dout(dc::action, "After calling sem_wait, " << s_semaphore);
    }
  };

  struct PriorityQueue : public AIObjectQueue<std::function<bool()>>
  {
    int const m_previous_total_reserved_threads;// The number of threads that are reserved for all higher priority queues together.
    int const m_total_reserved_threads;         // The number of threads that are reserved for this queue, plus all higher priority queues, together.
    std::atomic_int m_available_workers;        // The number of workers that may be added to work on queues of lower priority
                                                // (number of worker threads minus m_total_reserved_threads minus the number of
                                                //  worker threads that already work on lower priority queues).
                                                // The lowest priority queue must have a value of 0.
    // m_task_action is mutable because it must be changed in const member functions:
    // The 'const' there means 'by multiple threads at the same time' (aka "read access").
    // The non-const member functions of Action are thread-safe.
    mutable Action m_task_action;               // Keep track of number of actions required (number of tasks in the queue).

    PriorityQueue(int capacity, int previous_total_reserved_threads, int reserved_threads) :
        AIObjectQueue<std::function<bool()>>(capacity),
        m_previous_total_reserved_threads(previous_total_reserved_threads),
        m_total_reserved_threads(previous_total_reserved_threads + reserved_threads),
        m_available_workers(AIThreadPool::instance().number_of_workers() - m_total_reserved_threads)
        COMMA_DEBUG_ONLY(m_task_action("\"Task queue #" + std::to_string(m_previous_total_reserved_threads) + "\""))
            // m_previous_total_reserved_threads happens to be equal to the queue number, provided each queue on reserves one thread :/.
      { }

    PriorityQueue(PriorityQueue&& rvalue) :
        AIObjectQueue<std::function<bool()>>(std::move(rvalue)),
        m_previous_total_reserved_threads(rvalue.m_previous_total_reserved_threads),
        m_total_reserved_threads(rvalue.m_total_reserved_threads),
        m_available_workers(rvalue.m_available_workers.load())
        COMMA_DEBUG_ONLY(m_task_action(std::move(rvalue.m_task_action)))
      { }

    void available_workers_add(int n) { m_available_workers.fetch_add(n, std::memory_order_relaxed); }
    // As with AIObjectQueue, the 'const' here means "safe under concurrent access".
    // Therefore the const casst is ok, because the atomic m_available_workers is thread-safe.
    // Return true when the number of workers active on this queue may no longer be reduced.
    bool decrement_active_workers() const { return const_cast<std::atomic_int&>(m_available_workers).fetch_sub(1, std::memory_order_relaxed) <= 0; }
    void increment_active_workers() const { const_cast<std::atomic_int&>(m_available_workers).fetch_add(1, std::memory_order_relaxed); }
    int get_total_reserved_threads() const { return m_total_reserved_threads; }

    /*!
     * @brief Wake up one thread to process the just added function, if needed.
     *
     * When the threads of the thread pool have nothing to do, they go to
     * sleep by waiting on a semaphore. Call this function every time a new
     * message was added to a queue in order to make sure that there is a
     * thread that will handle it.
     *
     * This function is const because it is OK if multiple threads call it at
     * the same time and therefore accessed as part of getting a "read" lock,
     * which only gives const access.
     */
    void notify_one() const
    {
      Dout(dc::action, "Calling m_task_action.required() [Task queue #" << m_previous_total_reserved_threads << "]");
      m_task_action.required();
    }

    void still_required() const
    {
      Dout(dc::action, "Calling m_task_action.still_required() [Task queue #" << m_previous_total_reserved_threads << "]");
      m_task_action.still_required();
    }

    /*!
     * @brief If a task is available then take ownership.
     *
     * If this function returns true then the current thread is responsible
     * for executing one task from the queue.
     *
     * This function is const because it is OK if multiple threads call it at
     * the same time and therefore accessed as part of getting a "read" lock,
     * which only gives const access.
     */
    bool task_available(int& duty) const
    {
      return m_task_action.available(duty);
    }
  };

  // The life cycle of a Quit object:
  // - There is one Quit object per Worker and thus per (thread pool) thread.
  // - The Quit object is created with quit_ptr == nullptr before the thread is created.
  //   From this moment on forward it is possible that quit() is called.
  // - Once the thread is initialized and the std::atomic_bool exists, Quit::running is called.
  // - The thread never exits (and thus quit_ptr stays valid) until Quit::quit() was called.
  // Hence,
  // state 1: quit_ptr == nullptr and quit_called == false.
  // state 2: quit_ptr == adress of valid std::atomic_bool which is false and quit_called == false.
  // state 3: quit_ptr != nullptr (nl. adress of valid std::atomic_bool which is true) and quit_called == true.
  // state 4: quit_ptr != nullptr but invalid and quit_called == true.
  // state 5: quit_ptr == nullptr and quit_called == true.
  //
  // 1 -(running called)-> 2 -(quit called)-> 3 -(thread exited)-> 4.
  // or
  // 1 -(quit called)-> 5 -(running called)-> 3 -(thread exited)-> 4.
  //
  // cleanly_terminated() is for debugging purposes and tests that both running() and quit() were called.
  struct Quit
  {
    bool m_quit_called;                 // Set after calling quit().
    std::atomic_bool* m_quit_ptr;       // Only valid while m_quit_called is false (or while we're still inside Worker::main()).

    Quit() : m_quit_called(false), m_quit_ptr(nullptr) { }

    bool cleanly_terminated() const { return m_quit_ptr && m_quit_called; }
    bool quit_called() const { return m_quit_called; }

    void running(std::atomic_bool* quit)
    {
      m_quit_ptr = quit;
      *m_quit_ptr = m_quit_called;
    }

    void quit()
    {
      if (m_quit_ptr)
        m_quit_ptr->store(true, std::memory_order_relaxed);
      // From this point on we might leave Worker::main() which makes m_quit invalid.
      m_quit_called = true;
    }
  };

  struct Worker
  {
    using quit_t = aithreadsafe::Wrapper<Quit, aithreadsafe::policy::Primitive<std::mutex>>;
    // A Worker is only const when we access it from a const worker_container_t.
    // However, the (read) lock on the worker_container_t only protects the internals
    // of the container, not its elements. So, all elements are considered mutable.
    mutable quit_t m_quit;              // Create m_quit before m_thread.
    mutable std::thread m_thread;
#ifdef CWDEBUG
    std::thread::native_handle_type m_thread_id;
#endif

    // Construct a new Worker; do not associate it with a running thread yet.
    // A write lock on m_workers is required before calling this constructor;
    // that then blocks the thread from accessing m_quit until that lock is released
    // so that we have time to move the Worker in place (using emplace_back()).
    Worker(worker_function_t worker_function, int self) :
        m_thread(std::bind(worker_function, self)) COMMA_DEBUG_ONLY(m_thread_id(m_thread.native_handle())) { }

    // The move constructor can only be called as a result of a reallocation, as a result
    // of a size increase of the std::vector<Worker> (because Worker`s are put into it with
    // emplace_back(), Worker is not copyable and we never move a Worker out of the vector).
    // That means that at the moment the move constuctor is called we have the exclusive
    // write lock on the vector and therefore no other thread can access this Worker.
    // It is therefore safe to simply copy m_quit.
    Worker(Worker&& rvalue) : m_thread(std::move(rvalue.m_thread))
    {
      quit_t::wat quit_w(m_quit);
      quit_t::rat rvalue_quit_w(rvalue.m_quit);
      quit_w->m_quit_called = rvalue_quit_w->m_quit_called;
      quit_w->m_quit_ptr = rvalue_quit_w->m_quit_ptr;
      rvalue_quit_w->m_quit_called = false;
      rvalue_quit_w->m_quit_ptr = nullptr;
    }

    // Destructor.
    ~Worker()
    {
      DoutEntering(dc::threadpool, "~Worker() [" << (void*)this << "][" << std::hex << m_thread_id << "]");
      // Call quit() before destructing a Worker.
      ASSERT(quit_t::rat(m_quit)->cleanly_terminated());
      // Call join() before destructing a Worker.
      ASSERT(!m_thread.joinable());
    }

   public:
    // Set thread to running.
    void running(std::atomic_bool* quit) const { quit_t::wat(m_quit)->running(quit); }

    // Inform the thread that we want it to stop running.
    void quit() const
    {
      Dout(dc::threadpool, "Calling Worker::quit() [" << (void*)this << "]");
      quit_t::wat(m_quit)->quit();
    }

    // Wait for the thread to have exited.
    void join() const
    {
      // Call quit() before calling join().
      ASSERT(quit_t::rat(m_quit)->quit_called());
      // Only call join() once (this should be true for all Worker`s that were created and not moved).
      ASSERT(m_thread.joinable());
      m_thread.join();
    }

    // The main function for each of the worker threads.
    static void main(int const self);

    // Called from worker thread.
    static int get_handle();
  };

  // Number of idle workers.
  static std::atomic_int s_idle_threads;

  // Number of times that update_RunningTimers::update_current_timer() needs to be called.
  static Action s_call_update_current_timer;

  // Define a read/write lock protected container with all Worker`s.
  //
  // Obtaining and releasing a read lock by constructing and destructing a workers_t::rat object,
  // takes 178 ns (without optimization) / 117 ns (with optimization) [measured with microbench
  // on a 3.6GHz AMD FX(tm)-8150 core].
  //
  // [ Note that construcing and destructing a workers_t::wat object, for write access, takes 174 ns
  //   respectively 121 ns; although speed is not relevant in that case. ]
  //
  workers_t m_workers;

  // Mutex to protect critical areas in which conversion from read to write lock is necessary
  // (allowing concurrent conversion attempts can cause an exception to be thrown that we
  // can't recover from in our case).
  std::mutex m_workers_r_to_w_mutex;

  // Add new threads to the already write locked m_workers container.
  void add_threads(workers_t::wat& workers_w, int n);

  // Remove threads from the already read locked m_workers container.
  void remove_threads(workers_t::rat& workers_r, int n);

 public:
  //! The container type in which the queues are stored.
  using queues_container_t = utils::Vector<PriorityQueue, AIQueueHandle>;

 private:
  static std::atomic<AIThreadPool*> s_instance;               // The only instance of AIThreadPool that should exist at a time.
  // m_queues is seldom write locked and very often read locked, so use AIReadWriteSpinLock.
  using queues_t = aithreadsafe::Wrapper<queues_container_t, aithreadsafe::policy::ReadWrite<AIReadWriteSpinLock>>;
  queues_t m_queues;                                          // Vector of PriorityQueue`s.
  std::thread::id m_constructor_id;                           // Thread id of the thread that created and/or moved AIThreadPool.
  int m_max_number_of_threads;                                // Current capacity of m_workers.
  bool m_pillaged;                                            // If true, this object was moved and the destructor should do nothing.

 public:
  /*!
   * Construct an AIThreadPool with \a number_of_threads number of threads.
   *
   * @param number_of_threads The initial number of worker threads in this pool.
   * @param max_number_of_threads The largest value that you expect to pass to \ref change_number_of_threads_to during the execution of the program.
   */
  AIThreadPool(int number_of_threads = std::thread::hardware_concurrency() - 2, int max_number_of_threads = std::thread::hardware_concurrency());

  //! Copying is not possible.
  AIThreadPool(AIThreadPool const&) = delete;

  /*!
   * @brief Move constructor.
   *
   * The move constructor is not thread-safe. Usage is only intended to be used
   * directly after creation of the AIThreadPool, by the thread that created it,
   * to move it into place, if needed.
   */
  AIThreadPool(AIThreadPool&& rvalue) :
      m_constructor_id(rvalue.m_constructor_id),
      m_max_number_of_threads(rvalue.m_max_number_of_threads),
      m_pillaged(false)
  {
    // The move constructor is not thread-safe. Only the thread that constructed us may move us.
    assert(aithreadid::is_single_threaded(m_constructor_id));
    rvalue.m_pillaged = true;
    // Move the queues_container_t.
    *queues_t::wat(m_queues) = std::move(*queues_t::wat(rvalue.m_queues));
    // Once we're done with constructing this object, other threads (that likely still have to be started,
    // but that is not enforced) should be allowed to call AIThreadPool::instance(). In order to enforce
    // that all initialization of this object will be visible to those other threads, we need to prohibit
    // that stores done before this point arrive in such threads reordered to after this store.
    s_instance.store(this, std::memory_order_release);
  }

  //! Destructor terminates all threads and joins them.
  ~AIThreadPool();

  //------------------------------------------------------------------------
  // Threads management.

  /*!
   * @brief Change the number of threads.
   *
   * You bought more cores and updated it while running your program.
   *
   * @param number_of_threads The new number of threads.
   */
  void change_number_of_threads_to(int number_of_threads);

  /*!
   * @brief Return the number of worker threads.
   */
  int number_of_workers() const { return workers_t::crat(m_workers)->size(); }

  //------------------------------------------------------------------------
  // Queue management.

  //! Lock m_queues and get access (return value is to be passed to \ref get_queue).
  AIThreadPool::queues_t::rat queues_read_access() { return m_queues; }

  /*!
   * @brief Create a new queue.
   *
   * @param capacity The capacity of the new queue.
   * @param reserved_threads The number of threads that are rather idle than work on lower priority queues.
   *
   * The new queue is of a lower priority than all previously created queues.
   * The priority is determined by two things: the order in which queues are
   * searched for new tasks and the fact that \a reserved_threads threads
   * won't work on tasks of a lower priority (if any). Hence, passing a value
   * of zero to \a reserved_threads only has influence on the order in which
   * the tasks are processed, while using a larger value reduces the number
   * of threads that will work on lower priority tasks.
   *
   * @returns A handle for the new queue.
   */
  AIQueueHandle new_queue(int capacity, int reserved_threads = 1);

  /*!
   * @brief Return a reference to the queue that belongs to \a queue_handle.
   *
   * The returned pointer is only valid until a new queue is requested, which
   * is blocked only as long as \a queues_r isn't destructed: keep the read-access
   * object around until the returned reference is no longer used.
   *
   * Note that despite using a Read Access Type (rat) this function returns
   * a non-const reference! The reasoning is that "read access" here should be
   * interpreted as "may be accessed by an arbitrary number of threads at the
   * same time".
   *
   * @param queues_r The read-lock object as returned by \ref queues_read_access.
   * @param queue_handle An AIQueueHandle as returned by \ref new_queue.
   *
   * @returns A reference to AIThreadPool::PriorityQueue.
   */
  queues_container_t::value_type const& get_queue(queues_t::rat& queues_r, AIQueueHandle queue_handle) { return queues_r->at(queue_handle); }

  /*!
   * @brief Cause a call to RunningTimers::update_current_timer() (possibly by another thread).
   * Called from the timer signal handler.
   */
  static void call_update_current_timer()
  {
    //write(1, "\nCalling s_call_update_current_timer.required()\n", 48);
    s_call_update_current_timer.required();
  }

  //------------------------------------------------------------------------

  /*!
   * @brief Obtain a reference to the thread pool.
   *
   * Use this in threads that did not create the thread pool.
   */
  static AIThreadPool& instance()
  {
    // Construct an AIThreadPool somewhere, preferably at the beginning of main().
    ASSERT(s_instance.load(std::memory_order_relaxed) != nullptr);
    // In order to see the full construction of the AIThreadPool instance, we need to prohibit
    // reads done after this point from being reordered before this load, because that could
    // potentially still read memory locations in the object from before when it was constructed.
    return *s_instance.load(std::memory_order_acquire);
  }
};
