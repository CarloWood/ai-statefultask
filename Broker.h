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
    Broker_done
  };

 public:
  static state_type constexpr state_end = Broker_done + 1;      // The last state plus one.

 private:
  using unordered_map_type = std::unordered_map<statefultask::BrokerKey::unique_ptr, boost::intrusive_ptr<AIStatefulTask>, statefultask::BrokerKeyHash, statefultask::BrokerKeyEqual>;
  using map_type = aithreadsafe::Wrapper<unordered_map_type, aithreadsafe::policy::ReadWrite<AIReadWriteMutex>>;
  map_type m_key2connection;

 protected:
  ~Broker() override = default;
  char const* state_str_impl(state_type run_state) const override;
  void multiplex_impl(state_type run_state) override;
  void finish_impl() override;

 public:
  Broker(CWDEBUG_ONLY(bool debug = false)) : AIStatefulTask(CWDEBUG_ONLY(debug)) { }

  void run(AIStatefulTask::Handler handler = AIStatefulTask::Handler::immediate) { AIStatefulTask::run(handler); }

  // The returned pointer is meant to keep the task alive, not to access it (it is possibly shared between threads).
  // Read access is allowed only after (during) the callback was called.
  boost::intrusive_ptr<TaskType const> run(statefultask::BrokerKey const& key, std::function<void(bool)> callback);
};

template<typename TaskType, typename T>
boost::intrusive_ptr<TaskType const> Broker<TaskType, T>::run(statefultask::BrokerKey const& key, std::function<void(bool)> callback)
{
  map_type::wat key2connection_w(m_key2connection);
  auto search = key2connection_w->find(const_cast<statefultask::BrokerKey&>(key).non_owning_ptr());
  if (search != key2connection_w->end())
    return static_pointer_cast<TaskType const>(search->second);
  auto task = statefultask::create<TaskType>(CWDEBUG_ONLY(mSMDebug));
  (*key2connection_w)[key.copy()] = task;
  key.initialize(task);
  return task;
}

template<typename TaskType, typename T>
char const* Broker<TaskType, T>::state_str_impl(state_type run_state) const
{
  switch (run_state)
  {
    AI_CASE_RETURN(Broker_start);
    AI_CASE_RETURN(Broker_done);
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
      //task->run(callback);
      wait(1);
      break;
    case Broker_done:
      finish();
      break;
  }
}

template<typename TaskType, typename T>
void Broker<TaskType, T>::finish_impl()
{
}

} // namespace task
