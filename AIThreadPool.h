/**
 * @file
 * @brief Thread pool implementation.
 *
 * Copyright (C) 2017  Carlo Wood.
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
#include "debug.h"
#include "threadsafe/AIReadWriteMutex.h"
#include "threadsafe/aithreadid.h"
#include "threadsafe/aithreadsafe.h"
#include <thread>
#include <cassert>

// Only one AIThreadPool may exist at a time; it can be accessed by
// a call to a static function AIThreadPool::instance().
//
// However, an AIThreadPool is not a singleton; it doesn't have
// private constructors and it may not be constructed before main().
// Also, it has a public move constructor.
//
// It is allowed to create an AIThreadPool and after some usage
// destruct it; and then create a new one (this is not recommended).
// The recommended usage is:
//
// int main()
// {
// #ifdef DEBUGGLOBAL
//   GlobalObjectManager::main_entered();
// #endif
//   Debug(NAMESPACE_DEBUG::init());
//
//   AIAuxiliaryThread::start();
//   AIThreadPool thread_pool;          // Creates (std::thread::hardware_concurrency() - 2) threads by default:
//                                      // the number of concurrent threads supported by the implementation
//                                      // minus one for the main thread and minus one for the auxiliary thread.
// ...
//   // Use thread_pool or AIThreadPool::instance()
//
//   AIAuxiliaryThread::stop();
// }
//
class AIThreadPool {
  private:
    struct Worker;
    using worker_function_t = void (*)(int const);
    using worker_container_t = std::vector<Worker>;
    using workers_t = aithreadsafe::Wrapper<worker_container_t, aithreadsafe::policy::ReadWrite<AIReadWriteMutex>>;

    struct Worker {
      // A Worker is only const when we access it from a const worker_container_t.
      // However, the (read) lock on the worker_container_t only protects the internals
      // of the container, not it's elements. So, all elements are considered mutable.
      mutable std::thread m_thread;
      mutable std::atomic_bool m_quit;

      // Construct a new Worker; do not associate it with a running thread yet.
      // A write lock on m_workers is required before calling this constructor;
      // that then blocks the thread from accessing m_quit until that lock is released
      // so that we have time to move the Worker in place (using emplace_back()).
      Worker(worker_function_t worker_function, int self) : m_thread(std::bind(worker_function, self)), m_quit(false) { }

      // The move constructor can only be called as a result of a reallocation, as a result
      // of a size increase of the std::vector<Worker> (because Workers are put into it with
      // emplace_back(), Worker is not copyable, and we never move a Worker out of the vector).
      // That means that at the moment the move constuctor is called we have the exclusive
      // write lock on the vector and therefore no other thread can access this Worker.
      // Therefore it is safe to non-atomically copy m_quit (note that it cannot be moved or
      // copied atomically).
      Worker(Worker&& rvalue) : m_thread(std::move(rvalue.m_thread)), m_quit(rvalue.m_quit.load()) { rvalue.m_quit.store(true, std::memory_order_relaxed); }

      // Destructor.
      ~Worker()
      {
        // It's ok to use memory_order_relaxed here because this is the
        // same thread that (should have) called quit() in the first place.
        // Call quit() before destructing a Worker.
        ASSERT(m_quit.load(std::memory_order_relaxed));
        // Call join() before destructing a Worker.
        ASSERT(!m_thread.joinable());
      }

     public:
      // Inform the thread that we want it to stop running.
      void quit() const { m_quit.store(true, std::memory_order_relaxed); }

      // Wait for the thread to have exited.
      void join() const
      {
        // It's ok to use memory_order_relaxed here because this is the same thread that (should have) called quit() in the first place.
        // Only call join() on Workers that are quitting.
        ASSERT(m_quit.load(std::memory_order_relaxed));
        // Only call join() once (this should be true for all Worker's that were created and not moved).
        ASSERT(m_thread.joinable());
        m_thread.join();
      }

      // The main function for each of the worker threads.
      static void main(int const self);

      // Called from worker thread.
      static int get_handle();
      bool running() const { return !m_quit.load(std::memory_order_acquire); } // We are running as long as m_quit isn't set.
    };

    // Define a read/write lock protected container with all Workers.
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
    static void add_threads(workers_t::wat& workers_w, int current_number_of_threads, int requested_number_of_threads);

    // Remove threads from the already read locked m_workers container.
    static void remove_threads(workers_t::rat& workers_r, int n);

  private:
    static std::atomic<AIThreadPool*> s_instance;       // The only instance of AIThreadPool that should exist at a time.
    std::thread::id m_constructor_id;                   // Thread id of the thread that created and/or moved AIThreadPool.
    int m_max_number_of_threads;
    bool m_pillaged;

  public:
    AIThreadPool(int number_of_threads = std::thread::hardware_concurrency() - 2, int max_number_of_threads = std::thread::hardware_concurrency());
    AIThreadPool(AIThreadPool const&) = delete;
    AIThreadPool(AIThreadPool&& rvalue) : m_max_number_of_threads(rvalue.m_max_number_of_threads), m_pillaged(false)
    {
      // The move constructor is not thread-safe. Only the thread that constructed us may move us.
      assert(aithreadid::is_single_threaded(m_constructor_id));
      rvalue.m_pillaged = true;
      // Once we're done with constructing this object, other threads (that likely still have to be started,
      // but that is not enforced) should be allowed to call AIThreadPool::instance(). In order to enforce
      // that all initialization of this object will be visible to those other threads, we need to prohibit
      // that stores done before this point arrive in such threads reordered to after this store.
      s_instance.store(this, std::memory_order_release);
    }
    // Destructor terminates all threads and joins them.
    ~AIThreadPool();

    // You bought more cores and updated it while running your program.
    void change_number_of_threads_to(int number_of_threads);

    static AIThreadPool& instance()
    {
      // Construct an AIThreadPool somewhere, preferably at the beginning of main().
      ASSERT(s_instance != nullptr);
      // In order to see the full construction of the AIThreadPool instance, we need to prohibit
      // reads done after this point from being reordered before this load, because that could
      // potentially still read memory locations in the object from before when it was constructed.
      return *s_instance.load(std::memory_order_acquire);
    }
};

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
extern channel_ct threadpool;
NAMESPACE_DEBUG_CHANNELS_END
#endif
