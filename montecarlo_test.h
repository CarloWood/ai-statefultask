#pragma once

#include <cstdint>      // uint32_t

namespace montecarlo {

struct TaskState {
  int base_state;
  char const* base_state_str;
  uint32_t run_state;
  char const* run_state_str;
  uint32_t advance_state;
  char const* advance_state_str;
  bool blocked;
  bool reset;
  bool need_run;
  bool idle;
  bool skip_idle;
  bool aborted;
  bool finished;

  bool equivalent(TaskState const& task_state) const
  {
    return base_state == task_state.base_state &&
           run_state == task_state.run_state &&
           advance_state == task_state.advance_state &&
           blocked == task_state.blocked &&
           reset == task_state.reset &&
           idle == task_state.idle &&
           skip_idle == task_state.skip_idle &&
           aborted == task_state.aborted &&
           finished == task_state.finished;
  }
};

int const AIStatefulTask_cxx = 0;
int const AIStatefulTask_h = 10000;

void probe(int line, char const* description, TaskState state, int s1 = -1, char const* s1_str = nullptr, int s2 = -1, char const* s2_str = nullptr, int s3 = -1, char const* s3_str = nullptr);

} // namespace montecarlo
