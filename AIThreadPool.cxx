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
AIThreadPool::Action AIThreadPool::s_call_update_current_timer DEBUG_ONLY(("\"Timer\""));

//static
void AIThreadPool::Worker::main(int const self)
{
  Debug(NAMESPACE_DEBUG::init_thread());
  Dout(dc::threadpool, "Thread started.");
  AIThreadPool& thread_pool{AIThreadPool::instance()};

  // Unblock the POSIX signals that are used by the timer.
  sigprocmask(SIG_UNBLOCK, RunningTimers::instance().get_timer_sigset(), nullptr);

  // Wait until we have at least one queue.
  int count = 0;
  while (thread_pool.queues_read_access()->size() == 0)
  {
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    if (++count > 10000)
      DoutFatal(dc::fatal, "No thread pool queue found after 100 ms. Call thread_pool.new_queue() at least once immediately after creating the ThreadPool object!");
  }

  AIQueueHandle q;              // The current / next queue that this thread is processing.
  q.set_to_zero();              // Start with the highest priority queue (zero).
  std::function<bool()> task;   // The current / last task (moved out of the queue) that this thread is executing / executed.
  int duty = 0;                 // The number of tasks / actions that this thread performed since it woke up from sem_wait().

  std::atomic_bool quit;        // Keep this boolean outside of the Worker so it isn't necessary to lock m_workers every loop.

  // This must be a read lock. We are not allowed to write-lock m_workers because
  // at termination m_workers is read locked while joining threads and if that
  // happens before a thread reaches this point, it would deadlock.
  workers_t::rat(thread_pool.m_workers)->at(self).running(&quit);

  // The thread will keep running until Worker::quit() is called.
  while (!quit.load(std::memory_order_relaxed))
  {
    Dout(dc::threadpool, "Beginning of thread pool main loop (q = " << q << ')');

    Timer::time_point now;
    while (s_call_update_current_timer.available(duty))
    {
      Dout(dc::action, "Took ownership of timer action.");

      // First check if there have any timers expired.
      Timer* expired_timer;
      {
        // If this is (still) true then it wasn't reset by a thread that was
        // woken up by the signal handler, which means nothing except that
        // we were not just woken up.
        bool last_timer_expired = RunningTimers::instance().a_timer_expired();
        auto current_w{RunningTimers::instance().access_current()};
        if (last_timer_expired)
        {
          // This most likely happens when all threads were busy when the timer signal went off.
          Dout(dc::threadpool|flush_cf, "Timer " << (void*)current_w->timer << " did not wake up this thread.");
          current_w->timer = nullptr;     // The timer expired.
        }
        // Don't call update_current_timer when we're still waiting for the current timer to expire.
        // The reasoning here is: if the FIRST timer to expire didn't expire yet,
        // then we'll have no expired timers at all.
        if (AI_UNLIKELY(current_w->timer))
        {
          Dout(dc::notice, "Not calling update_current_timer because current_w->timer = " << (void*)current_w->timer);
          break;
        }
        // Use the same 'now' for subsequent calls; timers that expire after
        // we already reached this point will have to wait their turn.
        if (!now.time_since_epoch().count())
          now = Timer::clock_type::now();
        // Is there a(nother) timer that has expired before `now`?
        if (!(expired_timer = RunningTimers::instance().update_current_timer(current_w, now)))
        {
          // There is no timer, or
          // This thread just called timer_settime and set current_w->timer.
          // Other threads won't call update_current_timer anymore until that timer expired.
          break;
        }
      } // Do the call back with RunningTimers::m_current unlocked.
      call_update_current_timer();         // Keep calling update_current_timer until it returns nullptr.
      expired_timer->expire();
    }

    bool go_idle = false;
    { // Lock the queue for other consumer threads.
      auto queues_r = thread_pool.queues_read_access();
      // Obtain a reference to queue `q`.
      queues_container_t::value_type const& queue = (*queues_r)[q];
      bool empty = !queue.task_available(duty);
      if (!empty)
      {
        Dout(dc::action, "Took ownership of queue " << q << " action.");

        // Obtain and lock consumer access for this queue.
        auto access = queue.consumer_access();
#ifdef CWDEBUG
        // The number of messages in the queue.
        int length = access.length();
        // Only call queue.notify_one() when you just called queue_access.move_in(...).
        // For example, if the queue is full so you don't call move_in then ALSO don't call notify_one!
        ASSERT(length > 0);
#endif
        // Move one object from the queue to `task`.
        task = access.move_out();
      }
      else
      {
        // Note that at this point it is possible that queue.consumer_access().length() > 0 (aka, the queue is NOT empty);
        // if that is the case than another thread is between the lines 'bool empty = !queue.task_available(duty)'
        // and 'auto access = queue.consumer_access()'. I.e. it took ownership of the task but didn't extract it from
        // the queue yet.

        // Process lower priority queues if any.
        // We are not available anymore to work on lower priority queues: we're already going to work on them.
        // However, go idle if we're not allowed to process queues with a lower priority.
        if ((go_idle = queue.decrement_active_workers()))
          queue.increment_active_workers();             // Undo the above decrement.
        else if (!(go_idle = ++q == queues_r->iend()))  // If there is no lower priority queue left, then just go idle.
        {
          Dout(dc::threadpool, "Continuing with next queue.");
          continue;                                     // Otherwise, handle the lower priority queue.
        }
        if (go_idle)
        {
          // We're going idle and are available again for queues of any priority.
          // Increment the available workers on all higher priority queues again before going idle.
          while (q != queues_r->ibegin())
            (*queues_r)[--q].increment_active_workers();
        }
      } // Not empty - not idle - need to invoke the functor task().
    } // Unlock the queue.

    if (!go_idle)
    {
      Dout(dc::threadpool, "Not going idle.");

      bool active = true;
      AIQueueHandle next_q;

      while (active)
      {
        // ***************************************************
        active = task();   // Invoke the functor.            *
        // ***************************************************
        Dout(dc::threadpool, "task() returned " << active);

        // Determine the next queue to handle: the highest priority queue that doesn't have all reserved threads idle.
        next_q = q;
        { // Get read access to AIThreadPool::m_queues.
          auto queues_r = thread_pool.queues_read_access();
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
            // It must be the number of threads that are readily available to start
            // working on higher priority queues; which in itself means "can be
            // woken up instantly by a call to AIThreadPool::notify_one()".
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
            bool put_back;
            {
              auto pa = queue.producer_access();
              int length = pa.length();
              put_back = length < queue.capacity() && (next_q != q || length > 0);
              if (put_back)
              {
                // Put task() back into q.
                pa.move_in(std::move(task));
              }
            } // Unlock queue.
            if (put_back)
            {
              // Don't call required() because that also wakes up a thread
              // while this thread already goes to the top of the loop again.
              queue.still_required();
              break;
            }
            // Otherwise, call task() again.
          }
        }
      }
      q = next_q;
    }
    else // go_idle
    {
      Dout(dc::action(duty == 0), "Thread had nothing to do.");

      // Destruct the current task if any, to decrement the RefCount.
      task = nullptr;

      // A thread that enters this block has nothing to do.
      s_idle_threads.fetch_add(1);
      Action::wait();
      s_idle_threads.fetch_sub(1);
      if (RunningTimers::instance().a_timer_expired())
      {
        auto current_w{RunningTimers::instance().access_current()};
        Dout(dc::threadpool|flush_cf, "Timer " << (void*)current_w->timer << " woke up a thread.");
        current_w->timer = nullptr;     // The timer expired.
      }

      // We just left sem_wait(); reset 'duty' to count how many task we perform
      // for this single wake-up. Calling available() with a duty larger than
      // zero will call sem_trywait in an attempt to decrease the semaphore
      // count alongside the Action::m_required atomic counter.
      duty = 0;
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
  int const number_of_threads = workers_r->size();
  for (int i = 0; i < n; ++i)
    workers_r->at(number_of_threads - 1 - i).quit();
  // Wake up all threads, so the ones that need to quit can quit.
  for (int i = 0; i < number_of_threads; ++i)
    Action::wakeup();
  // If the relaxed stores to the quit atomic_bool`s is very slow
  // then we might be calling join() on threads before they can see
  // the flag being set. This should not be a problem but theoretically
  // the store could be delayed forever, so to be formerly correct
  // lets flush all stores here- before calling join().
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

//static
AIThreadPool::Action::Semaphore AIThreadPool::Action::s_semaphore;

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
channel_ct threadpool("THREADPOOL");
channel_ct action("ACTION");
NAMESPACE_DEBUG_CHANNELS_END
#endif
