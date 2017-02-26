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
};

int const AIStatefulTask_cxx = 0;
int const AIStatefulTask_h = 10000;

void probe(int line, char const* description, TaskState state, int s1 = -1, char const* s1_str = nullptr, int s2 = -1, char const* s2_str = nullptr, int s3 = -1, char const* s3_str = nullptr);

} // namespace montecarlo
