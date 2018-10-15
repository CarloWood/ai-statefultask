/**
 * @file
 * @brief Implementation of RunningTimers.
 *
 * @Copyright (C) 2018  Carlo Wood.
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
#include "RunningTimers.h"
#include "AIThreadPool.h"
#include "utils/macros.h"
#include <new>
#include <cstring>

extern "C" void timer_signal_handler(int)
{
  //write(1, "\nEntering timer_signal_handler()\n", 33);
  statefultask::RunningTimers::instance().set_a_timer_expired();
  AIThreadPool::call_update_current_timer();
  //write(1, "\nLeaving timer_signal_handler()\n", 32);
}

namespace statefultask {

RunningTimers::RunningTimers() : m_timer_signum(SIGRTMIN), m_a_timer_expired(false)
{
  // Initialize m_cache and m_tree.
  for (int interval = 0; interval < tree_size; ++interval)
  {
    m_cache[interval] = Timer::s_none;
    int parent_ti = interval_to_parent_index(interval);
    m_tree[parent_ti] = interval & ~1;
  }
  // Initialize the rest of m_tree.
  for (int index = tree_size / 2 - 1; index > 0; --index)
    m_tree[index] = m_tree[left_child_of(index)];

  // Call timer_signal_handler when the m_timer_signum signal is caught by a thread.
  struct sigaction action;
  std::memset(&action, 0, sizeof(struct sigaction));
  action.sa_handler = timer_signal_handler;
  if (sigaction(m_timer_signum, &action, NULL) == -1)
  {
    perror("sigaction");
    assert(false);
  }
  // Block the signals used by the thread pool (it is unblocked again for thread pool threads).
  sigemptyset(&m_timer_sigset);
  sigaddset(&m_timer_sigset, m_timer_signum);
  sigprocmask(SIG_BLOCK, &m_timer_sigset, nullptr);
}

RunningTimers::Current::Current() : timer(nullptr)
{
  // Create a monotonic timer.
  struct sigevent sigevent;
  std::memset(&sigevent, 0, sizeof(struct sigevent));
  sigevent.sigev_notify = SIGEV_SIGNAL;
  sigevent.sigev_signo = RunningTimers::instantiate().m_timer_signum;
  if (timer_create(CLOCK_MONOTONIC, &sigevent, &posix_timer) == -1)
  {
    perror("timer_create");
    DoutFatal(dc::fatal|error_cf, "timer_create");
  }
}

Timer::Handle RunningTimers::push(TimerQueueIndex interval, Timer* timer)
{
  DoutEntering(dc::notice, "RunningTimers::push(" << interval << ", " << (void*)timer << ")");
  assert(interval.get_value() < m_queues.size());
  uint64_t sequence;
  bool is_current;
  Timer::time_point expiration_point = timer->get_expiration_point();
  {
    timer_queue_t::wat queue_w(m_queues[interval]);
    sequence = queue_w->push(timer);
    is_current = queue_w->is_current(sequence);
    // Being 'current' means that it is the first timer in this queue.
    // Since the Handle for this timer isn't returned yet, it can't be
    // cancelled by another thread, so it will remain the current timer
    // until we leave this function. It is not necessary to keep the
    // queue locked.
    if (is_current)
      m_mutex.lock();
  }
  Timer::Handle handle{interval, sequence};
  if (is_current)
  {
    int cache_index = to_cache_index(interval);
    decrease_cache(cache_index, expiration_point);
    is_current = m_tree[1] == cache_index;
    m_mutex.unlock();
    if (is_current)
    {
      update_running_timer();
      AIThreadPool::instance().call_update_current_timer();
    }
  }
  return handle;
}

Timer* RunningTimers::update_current_timer(current_t::wat& current_w, Timer::time_point now)
{
  DoutEntering(dc::notice, "RunningTimers::update_current_timer(current_w, " << now.time_since_epoch().count() << ")");

  // Don't call this function while we have a current timer.
  ASSERT(current_w->timer == nullptr);
  bool need_update_cleared_and_current_unlocked = false;

  // Initialize interval, next and timer to correspond to the Timer in RunningTimers
  // that is the first to expire next, if any (if not then return nullptr).
  int interval;
  Timer::time_point next;
  Timer* timer;
  Timer::time_point::duration duration;
  m_mutex.lock();
  while (true)  // So we can use continue.
  {
    interval = m_tree[1];                     // The interval of the timer that will expire next.
    next = m_cache[interval];                 // The time at which it will expire.

    if (next == Timer::s_none)                // Is there a next timer at all?
    {
      m_mutex.unlock();
      if (AI_UNLIKELY(need_update_cleared_and_current_unlocked))
        current_w.relock(m_current);          // Lock m_current again.
      // current_w->timer is unset.
      Dout(dc::notice, "No timers.");
      return nullptr;                         // There is no next timer.
    }

    if (AI_LIKELY(!need_update_cleared_and_current_unlocked))
    {
      current_w.unlock();                     // Unlock m_current.
      need_update_cleared_and_current_unlocked = true;
    }

    m_mutex.unlock();                         // The queue must be locked first in order to avoid a deadlock.
    timer_queue_t::wat queue_w(m_queues[to_queues_index(interval)]);
    m_mutex.lock();                           // Lock m_mutex again.
    // Because of the short unlock of m_mutex, m_tree[1] and/or m_cache[interval] might have changed.
    next = m_cache[interval];
    if (AI_UNLIKELY(m_tree[1] != interval || next == Timer::s_none))    // Was there a race? Then try again.
      continue;
    duration = next - now;
    if (duration.count() <= 0)                // Did this timer already expire?
    {
      m_mutex.unlock();
      timer = queue_w->pop(m_mutex);
      Dout(dc::notice|flush_cf, "Timer " << (void*)timer << " expired " << -std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() << " ns ago.");
      increase_cache(interval, queue_w->next_expiration_point());
      m_mutex.unlock();
      current_w.relock(m_current);            // Lock m_current again.
      // current_w->timer is unset.
      Dout(dc::notice, "Expired timer.");
      return timer;                           // Do the call back.
    }
    Dout(dc::notice, "Timer did not expire yet.");
    timer = queue_w->peek();
    break;
  }
  m_mutex.unlock();

  // Calculate the timespec at which the current timer will expire.
  struct itimerspec new_value;
  memset(&new_value.it_interval, 0, sizeof(struct timespec));
  // This rounds down since duration is positive.
  auto s = std::chrono::duration_cast<std::chrono::seconds>(duration);
  new_value.it_value.tv_sec = s.count();
  auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - s);
  new_value.it_value.tv_nsec = ns.count();

  // Update the POSIX timer.
  Dout(dc::notice|flush_cf, "Calling timer_settime() for " << new_value.it_value.tv_sec << " seconds and " << new_value.it_value.tv_nsec << " nanoseconds.");
  current_w.relock(m_current);                  // Lock m_current again.
  sigprocmask(SIG_BLOCK, &m_timer_sigset, nullptr);
  [[maybe_unused]] bool pending = timer_settime(current_w->posix_timer, 0, &new_value, nullptr) == 0;
  ASSERT(pending);
  current_w->timer = timer;
  m_a_timer_expired = false;
  sigprocmask(SIG_UNBLOCK, &m_timer_sigset, nullptr);

  // Return nullptr, but this time with current_w->timer set.
  Dout(dc::notice|flush_cf, "Timer " << (void*)timer << " started.");
  return nullptr;
}

RunningTimers::~RunningTimers()
{
  DoutEntering(dc::notice, "RunningTimers::~RunningTimers() with m_queues.size() == " << m_queues.size());
  // Set all timers to 'not running', otherwise they call cancel() on us when they're being destructed.
  for (TimerQueueIndex interval = m_queues.ibegin(); interval != m_queues.iend(); ++interval)
    timer_queue_t::wat(m_queues[interval])->set_not_running();
}

} // namespace statefultask

namespace {
  SingletonInstance<statefultask::RunningTimers> dummy __attribute__ ((__unused__));
} // namespace
