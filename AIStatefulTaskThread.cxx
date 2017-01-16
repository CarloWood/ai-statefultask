/**
 * @file
 * @brief Implementation of AIStatefulTaskThread.
 *
 * Copyright (C) 2013, 2017  Carlo Wood.
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
 *   23/01/2013
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#if 0
#include "AIStatefulTaskThread.h"

class AIStatefulTaskThreadBase::Thread : public AIThread {
  private:
    boost::intrusive_ptr<AIStatefulTaskThreadBase> mImpl;
    bool mNeedCleanup;
  public:
    Thread(AIStatefulTaskThreadBase* impl) :
#ifdef DEBUG
      AIThread(impl->impl().getName()),
#else
      AIThread("AIStatefulTaskThreadBase::Thread"),
#endif
      mImpl(impl) { }
  protected:
    /*virtual*/ void run(void)
    {
      mNeedCleanup = mImpl->impl().thread_done(mImpl->impl().run());
    }
    /*virtual*/ void terminated(void)
    {
      mStatus = STOPPED;
      if (mNeedCleanup)
      {
        // The task that started us has disappeared! Clean up ourselves.

        // This is OK now because in our case nobody is watching us and having set
        // the status to STOPPED didn't change anything really, although it will
        // prevent AIThread::shutdown from blocking a whole minute and then calling
        // apr_thread_exit to never return! Normally, the AIThread might be deleted
        // immediately (by the main thread) after setting mStatus to STOPPED.
        Thread::completed(this);
      }
    }
  public:
    // TODO: Implement a thread pool. For now, just create a new one every time.
    static Thread* allocate(AIStatefulTaskThreadBase* impl) { return new Thread(impl); }
    static void completed(Thread* threadp) { delete threadp; }
};

// MAIN THREAD

char const* AIStatefulTaskThreadBase::state_str_impl(state_type run_state) const
{
  switch(run_state)
  {
    AI_CASE_RETURN(start_thread);
    AI_CASE_RETURN(wait_stopped);
  }
  return "UNKNOWN STATE";
}

void AIStatefulTaskThreadBase::initialize_impl(void)
{
  mThread = nullptr;
  mAbort = false;
  set_state(start_thread);
}

void AIStatefulTaskThreadBase::multiplex_impl(state_type run_state)
{
  switch(run_state)
  {
    case start_thread:
      mThread = Thread::allocate(this);
      // Set next state.
      set_state(wait_stopped);
      idle();                   // Wait till the thread returns.
      mThread->start();
      break;
    case wait_stopped:
      if (!mThread->isStopped())
      {
        yield();
        break;
      }
      // We're done!
      //
      // We can only get here when AIThreadImpl::done called cont(), (very
      // shortly after which it will be STOPPED), which means we need to do
      // the clean up of the thread.
      // Also, the thread has really stopped now, so it's safe to delete it.
      //
      // Clean up the thread.
      Thread::completed(mThread);
      mThread = nullptr;           // Stop abort_impl() from doing anything.
      if (mAbort)
        abort();
      else
        finish();
      break;
  }
}

void AIStatefulTaskThreadBase::abort_impl(void)
{
  if (mThread)
  {
    // If this AIStatefulTaskThreadBase still exists then the AIStatefulTaskThread<THREAD_IMPL>
    // that is derived from it still exists and therefore its member THREAD_IMPL also still exists
    // and therefore impl() is valid.
    bool need_cleanup = impl().stateful_task_done(mThread);     // Signal the fact that we aborted.
    if (need_cleanup)
    {
      // This is an unlikely race condition. We have been aborted by our parent,
      // but at the same time the thread finished!
      // We can only get here when AIThreadImpl::thread_done already returned
      // (VERY shortly after the thread will stop (if not already)).
      // Just go into a tight loop while waiting for that, when it is safe
      // to clean up the thread.
      while (!mThread->isStopped())
      {
        mThread->yield();
      }
      Thread::completed(mThread);
    }
    else
    {
      // The thread is still happily running (and will clean up itself).
      // Lets make sure we're not flooded with this situation.
      llwarns << "Thread task aborted while the thread is still running. That is a waste of CPU and should be avoided." << llendl;
    }
  }
}

bool AIThreadImpl::stateful_task_done(AIThread* threadp)
{
  StatefulTaskThread_wat stateful_task_thread_w(mStatefulTaskThread);
  AIStatefulTaskThreadBase* stateful_task_thread = *stateful_task_thread_w;
  bool need_cleanup = !stateful_task_thread;
  if (!need_cleanup)
  {
    // If stateful_task_thread is non-nullptr, then AIThreadImpl::thread_done wasnt called yet
    // (or at least didn't return yet) which means the thread is still running.
    // Try telling the thread that it can stop.
    threadp->setQuitting();
    // Finally, mark that we are NOT going to do the cleanup by setting mStatefulTaskThread to nullptr.
    *stateful_task_thread_w = nullptr;
  }
  return need_cleanup;
}

// AIStatefulTaskThread THREAD

bool AIThreadImpl::thread_done(bool result)
{
  StatefulTaskThread_wat stateful_task_thread_w(mStatefulTaskThread);
  AIStatefulTaskThreadBase* stateful_task_thread = *stateful_task_thread_w;
  bool need_cleanup = !stateful_task_thread;
  if (!need_cleanup)
  {
    // If stateful_task_thread is non-nullptr then AIThreadImpl::abort_impl wasn't called,
    // which means the task still exists. In fact, it should be in the waiting() state.
    // It can also happen that the task is being aborted right now.
    ASSERT(stateful_task_thread->waiting_or_aborting());
    stateful_task_thread->schedule_abort(!result);
    // Note that if the task is not running (being aborted, ie - hanging in abort_impl
    // waiting for the lock on mStatefulTaskThread) then this is simply ignored.
    stateful_task_thread->cont();
    // Finally, mark that we are NOT going to do the cleanup by setting mStatefulTaskThread to nullptr.
    *stateful_task_thread_w = nullptr;
  }
  return need_cleanup;
}
#endif
