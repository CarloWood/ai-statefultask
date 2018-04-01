/**
 * @file
 * @brief Implementation of AIThreadPool.
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
 */

#include "sys.h"
#include "debug.h"
#include "AIThreadPool.h"
#include "RunningTimers.h"
#include "Timer.h"
#include "utils/macros.h"
#include <csignal>
#ifdef CWDEBUG
#include <libcwd/type_info.h>
#endif

using Timer = statefultask::Timer;
using RunningTimers = statefultask::RunningTimers;

//static
std::atomic<AIThreadPool*> AIThreadPool::s_instance;

//static
std::atomic_int AIThreadPool::s_idle_threads;

//static
bool AIThreadPool::s_have_timer_thread;

//static
bool AIThreadPool::s_idle_timer_thread;

//static
std::mutex AIThreadPool::s_idle_mutex;

//static
std::condition_variable AIThreadPool::s_idle_cv;

//static
void AIThreadPool::Worker::main(int const self)
{
  Debug(NAMESPACE_DEBUG::init_thread());
  Dout(dc::threadpool, "Thread started.");

  // Wait until we have at least one queue.
  while (AIThreadPool::instance().queues_read_access()->size() == 0)
    std::this_thread::sleep_for(std::chrono::microseconds(10));

  std::function<bool()> f;
  AIQueueHandle q;
  q.set_to_zero();      // Zero is the highest priority queue.
  while (workers_t::rat(AIThreadPool::instance().m_workers)->at(self).running())
  {
    bool empty;
    bool go_idle = false;
    { // Lock the queue for other consumer threads.
      auto queues_r = AIThreadPool::instance().queues_read_access();
      // Obtain a reference to queue `q`.
      queues_container_t::value_type const& queue = (*queues_r)[q];
      {
        // Obtain and lock consumer access this queue.
        auto access = queue.consumer_access();
        // The number of messages in the queue.
        int length = access.length();
        empty = length == 0;
        // If the queue is not empty, move one object from the queue to `f`.
        if (!empty)
          f = access.move_out();
      }
      if (empty)
      {
        // Process lower priority queues if any.
        // We are not available anymore to work on lower priority queues: we're already going to work on them.
        // However, go idle if we're not allowed to process queues with a lower priority.
        if ((go_idle = queue.decrement_active_workers()))
          queue.increment_active_workers();             // Undo the above decrement.
        else if (!(go_idle = ++q == queues_r->iend()))  // If there is no lower priority queue left, then just go idle.
          continue;                                     // Otherwise, handle the lower priority queue.
        if (go_idle)
        {
          // We're going idle and are available again for queues of any priority.
          // Increment the available workers on all higher priority queues again before going idle.
          while (q != queues_r->ibegin())
            (*queues_r)[--q].increment_active_workers();
        }
      } // Not empty - not idle - need to invoke the functor f().
    } // Unlock the queue.

    if (!go_idle)
    {
      bool active = true;
      AIQueueHandle next_q;

      while (active)
      {
        // ***************************************************
        active = f();   // Invoke the functor.               *
        // ***************************************************

        // Determine the next queue to handle: the highest priority queue that doesn't have all reserved threads idle.
        next_q = q;
        { // Get read access to AIThreadPool::m_queues.
          auto queues_r = AIThreadPool::instance().queues_read_access();
          // See if any higher priority queues need our help.
          while (next_q != queues_r->ibegin())
          {
            // Obtain a reference to the queue with the next higher priority.
            queues_container_t::value_type const& queue = (*queues_r)[--next_q];

            // If the number of idle threads is greater or equal than the total
            // number of threads reserved for higher priority queues, then that
            // must mean that the higher priority queues are empty so that we
            // can stay on the current queue.
            //
            // Note that we're not locking s_idle_mutex because it doesn't matter
            // how precise the read value of s_idle_threads is here:
            // if the result of the comparison is false while it should have been
            // true then this thread simply will move to high priority queues,
            // find out that they are empty and quickly return to the lower
            // priority queue that it is on now. While, if the result of the
            // comparison is true while it should have been false, then this
            // thread will run a task of lower priority queue before returning
            // to the higher priority queue the next time.
            //
            // Nevertheless, this defines the canonical meaning of s_idle_threads:
            // It must be the number of threads are readily available to start
            // working on higher priority queues; which in itself means, can be
            // woken up instantly by a call to AIThreadPool::notify_one().
            // Since the thread that is performing timer call backs is not
            // readily available to be woken up for higher priority queues, it
            // should not contribute to the value of s_idle_threads. But since
            // the thread that is responsible for timer signals can be woken up
            // to handle a task, it DOES contribute to s_idle_threads while it
            // is suspended waiting for signals.
            if (s_idle_threads.load(std::memory_order_relaxed) >= queue.get_total_reserved_threads())
            {
              ++next_q;   // Stay on the last queue.
              break;
            }
            // Increment the available workers on this higher priority queue before checking it for new tasks.
            queue.increment_active_workers();
          }
          if (active)
          {
            queues_container_t::value_type const& queue = (*queues_r)[q];
            auto pa = queue.producer_access();
            int length = pa.length();
            if (length < queue.capacity() && (next_q != q || length > 0))
            {
              // Put f() back into q.
              pa.move_in(std::move(f));
              break;
            }
            // Otherwise, call f() again.
          }
        }
      }
      q = next_q;
    }
    else
    {
      // A thread that enters this block has nothing to do.
      // If a thread enters it that is the "only thread" in this block,
      // meaning that any thread that hasn't left this block yet will
      // leave it in order to check for another task and then reenter
      // it later once there is nothing to do again, then that thread
      // is responsible for handling timer events.
      // If other threads entered this block then they do not handle
      // timer events.
      // When a new task is added to the queue, and there are "other threads"
      // then one of those threads is woken up and shall leave this block,
      // while if there are no "other threads" then the first thread
      // that is handling timer events is woken up instead.
      // If a timer event happens while there are no threads in this
      // block then the event should be ignored because the first thread
      // that will enter this block will check for expired timers anyway.
      // If a timer event happens while there is at least one thread
      // in this block, then that thread has to re-check for expired
      // timer events. Since a timer event is a signal, and the signal
      // is only interesting when it happens after we called expire_next()
      //
      std::unique_lock<std::mutex> lk(s_idle_mutex);
      if (!s_have_timer_thread)
      {
        s_have_timer_thread = true;
        // Only one thread at a time can be in this block.                                                              //
        lk.unlock();                                                                                                    // The "timer thread".
        while (true)                                                                                                    //
        {                                                                                                               //
          //This thread is responsible for the next timer to expire, if any.                                            //
          Timer::time_point now = Timer::now();                                                                         //
          // Handle expired timers and get the next expiration point, if any.                                           //
          if (RunningTimers::instance().expire_next(now))                                                               //
          {                                                                                                             //
            // There is a running timer. Wait for it to expire.                                                         //
            //                                                                                                          //
            // Atomically increment s_idle_threads and go into the wait state                                           //
            // (atomically, because only one thread at a time can get here (due to s_have_timer_thread)                 //
            //  and signals are blocked at this point until we enter sigsuspend()).                                     //
            lk.lock();                                                                                                  //
            s_idle_threads.fetch_add(1, std::memory_order_relaxed);                                                     //
            s_idle_timer_thread = true;                                                                                 //
            lk.unlock();                                                                                                //
            RunningTimers::instance().wait_for_expire();                                                                //
            continue;                                                                                                   //
          }                                                                                                             //
          break;                                                                                                        //
        }                                                                                                               //
        lk.lock();                                                                                                      //
        s_have_timer_thread = false;                                                                                    //
        // We were woken up to process a task from a queue.
      }
      else
      {
        Dout(dc::notice, "Calling s_idle_cv.wait(lk) for other thread.");
        // Atomically increment s_idle_threads and go into the wait state.
        // The requirement we have here is that a thread that sees this increment will
        // not be able to obtain the lock on s_idle_mutex before this threads releases
        // it again inside s_idle_cv.wait(lk). In other words, a thread that sees the
        // increment must also see the mutex being locked. For that it is sufficient
        // that the increment is done with std::memory_order_relaxed.
        s_idle_threads.fetch_add(1, std::memory_order_relaxed);
        s_idle_cv.wait(lk);
        // One thread is woken up by AIThreadPool::notify_one(), which did the
        // decrement of s_idle_threads.
        Dout(dc::notice, "Returning from s_idle_cv.wait(lk) for other thread.");
      }
    }
  }

  Dout(dc::threadpool, "Thread terminated.");
}

void AIThreadPool::add_threads(workers_t::wat& workers_w, int n)
{
  DoutEntering(dc::threadpool, "add_threads(" << n << ")");
  {
    queues_t::wat queues_w(m_queues);
    for (auto&& queue : *queues_w)
      queue.available_workers_add(n);
  }
  int const current_number_of_threads = workers_w->size();
  for (int i = 0; i < n; ++i)
    workers_w->emplace_back(&Worker::main, current_number_of_threads + i);
}

// This function is called inside a criticial area of m_workers_r_to_w_mutex
// so we may convert the read lock to a write lock.
void AIThreadPool::remove_threads(workers_t::rat& workers_r, int n)
{
  DoutEntering(dc::threadpool, "remove_threads(" << n << ")");
  {
    queues_t::wat queues_w(m_queues);
    for (auto&& queue : *queues_w)
      queue.available_workers_add(-n);
  }

  // Since we only have a read lock on `m_workers` we can only
  // get access to const Worker's; in order to allow us to manipulate
  // individual Worker objects anywhere all their members are mutable.
  // This means that we need another mechanism to protect them
  // from concurrent access: all functions calling any method of
  // Worker must be inside the critical area of some mutex.
  // Both, quit() and join() are ONLY called in this function;
  // and this function is only called while in the critical area
  // of m_workers_r_to_w_mutex; so we're OK.

  // Call quit() on the n last threads in the container.
  int t = workers_r->size();
  for (int i = 0; i < n; ++i)
    workers_r->at(--t).quit();
  s_idle_cv.notify_all();
  // If the relaxed stores to the m_quit's is very slow then we might
  // be calling join() on threads before they can see their m_quit
  // flag being set. This is not a problem. However, theoretically
  // the store could be delayed forever, so to be formerly correct,
  // lets flush all stores here before calling join().
  std::atomic_thread_fence(std::memory_order_release);
  // Join the n last threads in the container.
  for (int i = 0; i < n; ++i)
  {
    // Worker threads need to take a read lock on m_workers, so it
    // would not be smart to have a write lock on that while trying
    // to join them.
    workers_r->back().join();
    // Finally, obtain a write lock and remove/destruct the Worker.
    workers_t::wat(workers_r)->pop_back();
  }
}

AIThreadPool::AIThreadPool(int number_of_threads, int max_number_of_threads) :
    m_constructor_id(aithreadid::none), m_max_number_of_threads(std::max(number_of_threads, max_number_of_threads)), m_pillaged(false)
{
  // Only here to record the id of the thread who constructed us.
  // Do not create a second AIThreadPool from another thread;
  // use AIThreadPool::instance() to access the thread pool created from main.
  assert(aithreadid::is_single_threaded(m_constructor_id));

  // Only construct ONE AIThreadPool, preferably somewhere at the beginning of main().
  // If you want more than one thread pool instance, don't. One is enough and much
  // better than having two or more.
  assert(s_instance == nullptr);

  // Allow access to the thread pool from everywhere without having to pass it around.
  s_instance = this;

  workers_t::wat workers_w(m_workers);
  assert(workers_w->empty());                    // Paranoia; even in the case of constructing a second AIThreadPool
                                                 // after destructing the first, this should be the case here.
  workers_w->reserve(m_max_number_of_threads);   // Attempt to avoid reallocating the vector in the future.
  add_threads(workers_w, number_of_threads);     // Create and run number_of_threads threads.
}

AIThreadPool::~AIThreadPool()
{
  // Construction and destruction is not thread-safe.
  assert(aithreadid::is_single_threaded(m_constructor_id));
  if (m_pillaged) return;                        // This instance was moved. We don't really exist.

  // Kill all threads.
  {
    std::lock_guard<std::mutex> lock(m_workers_r_to_w_mutex);
    workers_t::rat workers_r(m_workers);
    remove_threads(workers_r, workers_r->size());
  }
  // Allow construction of another AIThreadPool.
  s_instance = nullptr;
}

void AIThreadPool::change_number_of_threads_to(int requested_number_of_threads)
{
  if (requested_number_of_threads > m_max_number_of_threads)
  {
    Dout(dc::warning, "Increasing number of thread beyond the initially set maximum.");
    workers_t::wat workers_w(m_workers);
    // This might reallocate the vector and therefore move existing Workers,
    // but that should be ok while holding the write-lock... I think.
    workers_w->reserve(requested_number_of_threads);
    m_max_number_of_threads = requested_number_of_threads;
  }

  // This must be locked before locking m_workers.
  std::lock_guard<std::mutex> lock(m_workers_r_to_w_mutex);
  // Kill or add threads.
  workers_t::rat workers_r(m_workers);
  int const current_number_of_threads = workers_r->size();
  if (requested_number_of_threads < current_number_of_threads)
    remove_threads(workers_r, current_number_of_threads - requested_number_of_threads);
  else if (requested_number_of_threads > current_number_of_threads)
  {
    workers_t::wat workers_w(workers_r);
    add_threads(workers_w, requested_number_of_threads - current_number_of_threads);
  }
}

AIQueueHandle AIThreadPool::new_queue(int capacity, int reserved_threads)
{
  DoutEntering(dc::threadpool, "AIThreadPool::new_queue(" << capacity << ", " << reserved_threads << ")");
  queues_t::wat queues_w(m_queues);
  AIQueueHandle index(queues_w->size());
  int previous_reserved_threads = index.is_zero() ? 0 : (*queues_w)[index - 1].get_total_reserved_threads();
  queues_w->emplace_back(capacity, previous_reserved_threads, reserved_threads);
  Dout(dc::threadpool, "Returning index " << index << "; size is now " << queues_w->size() <<
      " for " << libcwd::type_info_of(*queues_w).demangled_name() << " at " << (void*)&*queues_w);
  return index;
}

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
channel_ct threadpool("THREADPOOL");
NAMESPACE_DEBUG_CHANNELS_END
#endif
