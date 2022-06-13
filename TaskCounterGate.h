#pragma once

#include "utils/macros.h"
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "debug.h"

namespace statefultask {

// This class is intended to detect at program termination when all tasks (that call `increment`
// in initialization_impl and `decrement` in finish_impl) are finished.
//
// It is only safe when no thread calls `increment` anymore once `wait` has been called.
// Therefore, thread that calls `wait` must make sure that no new tasks (that use this object)
// will be created anymore.
//
// Tasks that need to be waited for at program termination should call `increment` as soon as
// possible but no sooner than that it is guaranteed that they will also be run. Tasks that
// do not restart (by calling run() from the callback) can call `increment` from their constructor
// when their create and run are in the same scope. For example:
//
//   auto task = statefultask::create<task::Foo> foo(args);
//   ...
//   task->run(...);
//
// If a task might be restarted, or when they are created elsewhere and run later, should
// call `increment` from their initialization_impl. Because every class has an initialization_impl
// and finish_impl that are called anyway; it is best to always use those to call increment
// and decrement.
//
class TaskCounterGate
{
  using counter_type = uint32_t;
  static constexpr counter_type not_waiting_magic = 0x10000;    // Should be larger than the maximum number of simultaneous running tasks (that use this TaskCounterGate).
  static constexpr counter_type count_mask = not_waiting_magic - 1;
  std::mutex m_counter_is_zero_mutex;                           // Mutex used for the condition variable.
  std::condition_variable m_counter_is_zero;                    // Used to wait until m_counter became zero.
  std::atomic<counter_type> m_counter{not_waiting_magic};       // Count is set to a value larger than zero in order to stop decrement
                                                                // from calling wakeup() unless wait() has already been entered by another thread.
  void wakeup();

  [[gnu::always_inline]] bool is_waiting() const
  {
    return !(m_counter & not_waiting_magic);
  }

 public:
  // Call from initialize_impl().
  void increment()
  {
    // Never call increment() after wait() was already called.
    ASSERT(!is_waiting());
    m_counter.fetch_add(1, std::memory_order::relaxed);
  }

  // Call from finish_impl().
  void decrement()
  {
    counter_type previous_value = m_counter.fetch_sub(1, std::memory_order::relaxed);
    // Call increment() / decrement() in pairs.
    ASSERT(previous_value != 0);
    if (AI_UNLIKELY(previous_value == 1))       // Unlikely because normally the not_waiting_magic will be set.
      wakeup();
  }

  // Block until all remaining tasks finished / called decrement.
  void wait();
};

} // namespace statefultask
