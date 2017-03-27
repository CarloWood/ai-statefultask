/**
 * @file
 * @brief Run code in a thread. Declaration of template class AIStatefulTaskThread.
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

#pragma once

#include "AIStatefulTask.h"
#include "threadsafe/aithreadsafe.h"

#ifdef EXAMPLE_CODE     // undefined

class HelloWorldThread : public AIThreadImpl {
  private:
    bool mStdErr;               // input
    bool mSuccess;              // output

  public:
    // Constructor.
    HelloWorldThread() : AIThreadImpl("HelloWorldThread"),              // MAIN THREAD
        mStdErr(false), mSuccess(false) { }

    // Some initialization function (if needed).
    void init(bool err) { mStdErr = err; }                              // MAIN THREAD
    // Read back output.
    bool successful() const { return mSuccess; }

    // Mandatory signature.
    /*virtual*/ bool run()                                              // NEW THREAD
    {
      if (mStdErr)
        std::cerr << "Hello world" << std::endl;
      else
        std::cout << "Hello world" << std::endl;
      mSuccess = true;
      return true;              // true = finish, false = abort.
    }
};

// The states of this stateful task.
enum hello_world_state_type {
  HelloWorld_start = AIStatefulTask::max_state,
  HelloWorld_done
};

// The stateful task class (this is almost a template).
class HelloWorld : public AIStatefulTask {
  private:
    boost::intrusive_ptr<AIStatefulTaskThread<HelloWorldThread> > mHelloWorld;
    bool mErr;

  public:
    HelloWorld() : mHelloWorld(new AIStatefulTaskThread<HelloWorldThread>), mErr(false) { }

    // Print to stderr or stdout?
    void init(bool err) { mErr = err; }

  protected:
    // Call finish() (or abort()), not delete.
    /*virtual*/ ~HelloWorld() { }

    // Implemenation of state_str for run states.
    /*virtual*/ char const* state_str_impl(state_type run_state) const;

    // Handle initializing the object.
    /*virtual*/ void initialize_impl();

    // Handle run_state.
    /*virtual*/ void multiplex_impl(state_type run_state);
};

char const* HelloWorld::state_str_impl(state_type run_state) const
{
  switch(run_state)
  {
    AI_CASE_RETURN(HelloWorld_start);
    AI_CASE_RETURN(HelloWorld_done);
  }
  return "UNKNOWN STATE";
}

// The actual implementation of this task starts here!

void HelloWorld::initialize_impl()
{
  mHelloWorld->thread_impl().init(mErr);        // Initialize the thread object.
  set_state(HelloWorld_start);
}

void HelloWorld::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case HelloWorld_start:
      {
        mHelloWorld->run(this, HelloWorld_done);        // Run HelloWorldThread and set the state of 'this' to HelloWorld_done when finished.
        idle();                                         // Always go idle after starting a thread!
        break;
      }
    case HelloWorld_done:
      {
        // We're done. Lets also abort when the thread reported no success.
        if (mHelloWorld->thread_impl().successful())    // Read output/result of thread object.
          finish();
        else
          abort();
        break;
      }
  }
}

#endif  // EXAMPLE CODE

class AIStatefulTaskThreadBase;

// Derive from this to implement the code that must run in another thread.
class AIThreadImpl {
  private:
    template<typename THREAD_IMPL> friend class AIStatefulTaskThread;
    AIThreadSafeSimpleDC<AIStatefulTaskThreadBase*> mStatefulTaskThread;

  public:
    virtual bool run() = 0;
    bool thread_done(bool result);
    bool stateful_task_done(AIThread* threadp);

#ifdef DEBUG
  private:
    char const* mName;
  protected:
    AIThreadImpl(char const* name = "AIStatefulTaskThreadBase::Thread") : mName(name) { }
  public:
    char const* getName() const { return mName; }
#endif

  protected:
    virtual ~AIThreadImpl() { }
};

// The base class for stateful task threads.
class AIStatefulTaskThreadBase : public AIStatefulTask {
  private:
    // The actual thread (derived from AIThread).
    class Thread;

  protected:
    typedef AIStatefulTask direct_base_type;

    // The states of this stateful task.
    enum thread_state_type {
      start_thread = direct_base_type::max_state,       // Start the thread (if necessary create it first).
      wait_stopped                                      // Wait till the thread is stopped.
    };
  public:
    static state_type const max_state = wait_stopped + 1;

  protected:
    AIStatefulTaskThreadBase(CWD_ONLY(bool debug))
#ifdef CWDEBUG
      : AIStatefulTask(debug)
#endif
      { }

  private:
    // Implemenation of state_str for run states.
    /*virtual*/ char const* state_str_impl(state_type run_state) const;

    // Handle initializing the object.
    /*virtual*/ void initialize_impl();

    // Handle mRunState.
    /*virtual*/ void multiplex_impl(state_type run_state);

    // Handle aborting from current bs_run state.
    /*virtual*/ void abort_impl();

    // Returns a reference to the implementation code that needs to be run in the thread.
    virtual AIThreadImpl& impl() = 0;

  private:
    Thread* mThread;            // The thread that the code is run in.
    bool mAbort;                // (Inverse of) return value of AIThreadImpl::run(). Only valid in state wait_stopped.

  public:
    void schedule_abort(bool do_abort) { mAbort = do_abort; }

};

// A stateful task that executes T::run() in a thread.
// THREAD_IMPL Must be derived from AIThreadImpl.
template<typename THREAD_IMPL>
class AIStatefulTaskThread : public AIStatefulTaskThreadBase {
  private:
    THREAD_IMPL mThreadImpl;

  public:
    // Constructor.
    AIStatefulTaskThread(CWD_ONLY(bool debug))
#ifdef CWDEBUG
      : AIStatefulTaskThreadBase(debug)
#endif
      {
        *AIThreadImpl::StatefulTaskThread_wat(mThreadImpl.mStatefulTaskThread) = this;
      }

    // Accessor.
    THREAD_IMPL& thread_impl() { return mThreadImpl; }

  protected:
    /*virtual*/ AIThreadImpl& impl() { return mThreadImpl; }
};
