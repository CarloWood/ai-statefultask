#pragma once

#include "AIStatefulTask.h"
#include "BrokerKey.h"
#include "threadsafe/AIReadWriteMutex.h"
#include <type_traits>

namespace task {

template<typename TaskType, typename = typename std::enable_if<std::is_base_of<AIStatefulTask, TaskType>::value>::type>
class Broker : public AIStatefulTask
{
 protected:
  using direct_base_type = AIStatefulTask;      // The immediate base class of this task.

  // The different states of the task.
  enum broker_state_type {
    Broker_start = direct_base_type::state_end,
    Broker_do_work
  };

 public:
  static state_type constexpr state_end = Broker_do_work + 1;      // The last state plus one.

 private:
  struct CallbackQueue {
    std::vector<std::function<void(bool)>> m_queue;

    // m_success is initialized here because it is UB to read it before initialized,
    // and we might do that in run() in order to avoid a branch.
    // Reserve only a capacity of four, because it is very unlikely that even
    // more than one callback will be in the vector at the same time.
    CallbackQueue(std::function<void(bool)>&& callback)
    {
      m_queue.reserve(4);
      m_queue.emplace_back(std::move(callback));
    }
  };
  using callbacks_type = aithreadsafe::Wrapper<CallbackQueue, aithreadsafe::policy::Primitive<std::mutex>>;

  // Constness of this object means that we have got access to it through by read-locking m_key2task.
  // In most cases that read-lock is even released again by the time this object is accessed.
  //
  // Concurrent access is made safe in other ways.
  //
  // The members are made mutable because this access also involves writing.
  struct TaskPointerAndCallbackQueue {
    boost::intrusive_ptr<TaskType> const m_task;
    // Concurrent access is fine since callbacks_type is thread safe: access is protected by its own mutex.
    mutable callbacks_type m_callbacks;
    // Only when m_finished is loaded with acquire and is true, the TaskType that m_task points to and the boolean m_success may be read.
    // m_finished is initialized at false and only set to true once by the Broker task, using memory_order_release.
    // Other threads only read m_success after loading m_finished with memory_order_acquire and seeing that being true - which means that
    // it is safe for the Broker task to write to m_success before setting m_finished to true.
    mutable std::atomic<bool> m_finished;               // Set to true when the callback of the actual task is called.
    mutable bool m_success;                             // Set to the value passed to the actual callback of the task.
    // This variable is only accessed by the Broker task; and thus it is virtually single-threaded.
    mutable bool m_running;

    TaskPointerAndCallbackQueue(boost::intrusive_ptr<TaskType>&& task, std::function<void(bool)>&& callback) :
      m_task(std::move(task)), m_callbacks(std::move(callback)), m_finished(false), m_success(false), m_running(false) { }

#ifdef CWDEBUG
    void print_on(std::ostream& os) const
    {
      os << '"' << libcwd::type_info_of<TaskType>().demangled_name() << '"';
    }
#endif
  };
  using unordered_map_type = std::unordered_map<
      statefultask::BrokerKey::unique_ptr,
      TaskPointerAndCallbackQueue,
      statefultask::BrokerKeyHash,
      statefultask::BrokerKeyEqual>;
  using map_type = aithreadsafe::Wrapper<unordered_map_type, aithreadsafe::policy::ReadWrite<AIReadWriteMutex>>;

  map_type m_key2task;
  bool m_is_immediate;

 protected:
  ~Broker() override = default;
  char const* state_str_impl(state_type run_state) const override;
  void multiplex_impl(state_type run_state) override;

 public:
  Broker(CWDEBUG_ONLY(bool debug = false)) : AIStatefulTask(CWDEBUG_ONLY(debug)), m_is_immediate(false) { }

  void run(AIStatefulTask::Handler handler = AIStatefulTask::Handler::immediate)
  {
    // If the handler passed here is immediate, then a call to run(key, callback) while
    // the task is already finished will lead to an immediate call to the callback.
    // Otherwise the callback is performed from the Broken task (under the handler).
    m_is_immediate = handler.is_immediate();
    AIStatefulTask::run(handler, [](bool success){
        // Can only be terminated by calling abort().
        ASSERT(!success);
        Dout(dc::notice, "task::Broker<" << libcwd::type_info_of<TaskType>().demangled_name() << "> terminated.");
    });
  }

  // The returned pointer is meant to keep the task alive, not to access it (it is possibly shared between threads).
  // Read access is allowed only after (during) the callback was called.
  boost::intrusive_ptr<TaskType const> run(statefultask::BrokerKey const& key, std::function<void(bool)>&& callback);
};

template<typename TaskType, typename T>
boost::intrusive_ptr<TaskType const> Broker<TaskType, T>::run(statefultask::BrokerKey const& key, std::function<void(bool)>&& callback)
{
  // This function returns a pointer to an immutable TaskType, because the returned
  // task is shared between threads and readonly. Note reading it is only allowed
  // after the task finished running because otherwise writing may occur at the same
  // time, which is UB.
  // This must be a const* because we set it while only having a read lock on the unordered_map.
  typename unordered_map_type::mapped_type const* entry;
  // A boolean indicating if a task with the required key already existed or not.
  bool task_created;
  for (;;)
  {
    try
    {
      // Obtain a read-lock and read-access to m_key2task.
      typename map_type::rat key2task_r(m_key2task);
      // The cast is necessary because find() requires the non-const BrokerKey::unique_ptr reference
      // (as opposed to BrokerKey::const_unique_ptr). It is safe because find() will not alter the
      // BrokerKey pointed to.
      auto search = key2task_r->find(const_cast<statefultask::BrokerKey&>(key).non_owning_ptr());
      if ((task_created = search == key2task_r->end()))
      {
        // The task wasn't created yet.
        // In order to do so, we have to obtain the write lock first.
        typename map_type::wat key2task_w(key2task_r);                         // This might throw.
        // Create the task and put the boost::intrusive_ptr to it into the unordered_map together with a CallbackQueue object
        // already filled with callback, under key. Store the pointer to the new pair into entry.
        entry = &key2task_w->try_emplace(key.copy(), statefultask::create<TaskType>(CWDEBUG_ONLY(mSMDebug)), std::move(callback)).first->second;
      }
      else
      {
        // The task already exists. Store a pointer to the element in the unordered_map; note that
        // pointers (and references) to elements of an unorder_map are never invalidated, unless
        // the element itself is erased.
        entry = &search->second;
      }
      break;
    }
    catch (std::exception const&)
    {
      // Another thread is already trying to convert its read-lock into a write-lock.
      // Let that thread grab it and create the task.
      m_key2task.rd2wryield();
    }
  }
  if (task_created)
  {
    // It is safe to do this without a read or write lock on m_key2task, because no
    // other threads are accessing the (just created) TaskType. They are just waiting for
    // it to be finished.
    key.initialize(entry->m_task);
    // Wake up the Broker task.
    signal(1);
  }
  else
  {
    bool finished = entry->m_finished.load(std::memory_order_acquire);
    if (finished && m_is_immediate)
      callback(entry->m_success);
    else
    {
      // Queue the call back.
      typename callbacks_type::wat callbacks_w(entry->m_callbacks);
      callbacks_w->m_queue.emplace_back(std::move(callback));
      signal(1);
    }
  }
  return entry->m_task;
}

template<typename TaskType, typename T>
char const* Broker<TaskType, T>::state_str_impl(state_type run_state) const
{
  switch (run_state)
  {
    AI_CASE_RETURN(Broker_start);
    AI_CASE_RETURN(Broker_do_work);
  }
  ASSERT(false);
  return "UNKNOWN STATE";
}

template<typename TaskType, typename T>
void Broker<TaskType, T>::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case Broker_start:
      set_state(Broker_do_work);
      wait(1);
      break;
    case Broker_do_work:
    {
      // New callbacks have been added. A new task might also have been added however and needs to be run.
      // Get read access to the map with tasks.
      {
        typename map_type::rat key2task_r(m_key2task);
#ifdef CWDEBUG
        int size = key2task_r->size();
        Dout(dc::statefultask(mSMDebug), ((size == 1) ? "There is " : "There are ") << size << " registered task" << ((size == 1) ? "." : "s."));
#endif
        for (typename unordered_map_type::const_iterator it = key2task_r->begin(); it != key2task_r->end(); ++it)
        {
          TaskPointerAndCallbackQueue const& entry{it->second};
          Dout(dc::statefultask(mSMDebug)|continued_cf, "Processing entry " << entry << "; ");
          if (!entry.m_running)
          {
            entry.m_running = true;
            Dout(dc::statefultask(mSMDebug), "The task of this entry wasn't started yet. Calling run() now:");
            // Run the newly created task, causing a wake up of the Broker when it is done.
            entry.m_task->run(this, 1, signal_parent);
            Dout(dc::finish, "returned from run().");
          }
          else if (entry.m_task->finished())
          {
            entry.m_success = !entry.m_task->aborted();
            // The task finished.
            {
              // Obtain lock and read/write access to the CallbackQueue object of this task.
              typename callbacks_type::wat callbacks_w(entry.m_callbacks);
              DoutEntering(dc::statefultask(mSMDebug && !callbacks_w->m_queue.empty()), "for-loop with " << callbacks_w->m_queue.size() << " pending callbacks.");
              // Call all the callbacks that were registered so far.
              for (auto&& callback : callbacks_w->m_queue)
                callback(entry.m_success);
              // Make sure we won't call them again.
              callbacks_w->m_queue.clear();
              entry.m_finished.store(true, std::memory_order_release);
            }
            Dout(dc::finish, "callback queue cleared.");
          }
          else
            Dout(dc::finish, "skipping: not finished.");
        }
      }
      wait(1);
      break;
    }
  }
}

} // namespace task
