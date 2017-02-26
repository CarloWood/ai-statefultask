#include "sys.h"
#include "montecarlo_test.h"
#include "AIStatefulTask.h"
#include "threadsafe/aithreadid.h"
#include "debug.h"
#include <set>
#include <fstream>

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
channel_ct montecarlo("MONTECARLO");
NAMESPACE_DEBUG_CHANNELS_END
#endif

namespace montecarlo {

struct FullState {
  int line;
  char const* description;
  TaskState task_state;
  int s1;
  char const* s1_str;
  int s2;
  char const* s2_str;
  int s3;
  char const* s3_str;

  FullState(int _line, char const* _description, TaskState const& _task_state, int _s1, char const* _s1_str, int _s2, char const* _s2_str, int _s3, char const* _s3_str) :
      line(_line), description(_description), task_state(_task_state), s1(_s1), s1_str(_s1_str), s2(_s2), s2_str(_s2_str), s3(_s3), s3_str(_s3_str) { }
};

std::ostream& operator<<(std::ostream& os, FullState const& full_state)
{
  os << "{ '" << full_state.description << "' (" << std::dec << full_state.line << ")";
  if (full_state.s1 != -1) os << ' ' << full_state.s1_str;
  if (full_state.s2 != -1) os << '/' << full_state.s2_str;
  if (full_state.s3 != -1) os << '/' << full_state.s3_str;
  os << ", " << full_state.task_state.base_state_str << '/' << full_state.task_state.run_state_str;
  if (full_state.task_state.advance_state) os << '/' << full_state.task_state.advance_state_str << ",";
  if (full_state.task_state.need_run) os << " need_run";
  if (full_state.task_state.idle) os << " idle";
  if (full_state.task_state.skip_idle) os << " skip_idle";
  if (full_state.task_state.blocked) os << " blocked";
  if (full_state.task_state.reset) os << " reset";
  if (full_state.task_state.aborted) os << " aborted";
  if (full_state.task_state.finished) os << " finished";
  os << '}';
  return os;
}

bool operator<(FullState const& fs1,  FullState const& fs2)
{
  if (fs1.line != fs2.line)
    return fs1.line < fs2.line;
  if (fs1.s1 !=  fs2.s1)
    return fs1.s1 < fs2.s1;
  if (fs1.s2 !=  fs2.s2)
    return fs1.s2 < fs2.s2;
  if (fs1.s3 !=  fs2.s3)
    return fs1.s3 < fs2.s3;
  if (fs1.task_state.base_state != fs2.task_state.base_state)
    return fs1.task_state.base_state < fs2.task_state.base_state;
  if (fs1.task_state.run_state != fs2.task_state.run_state)
    return fs1.task_state.run_state < fs2.task_state.run_state;
  if (fs1.task_state.advance_state != fs2.task_state.advance_state)
    return fs1.task_state.advance_state < fs2.task_state.advance_state;
  if (fs1.task_state.blocked != fs2.task_state.blocked)
    return fs2.task_state.blocked;
  if (fs1.task_state.reset != fs2.task_state.reset)
    return fs2.task_state.reset;
  if (fs1.task_state.need_run != fs2.task_state.need_run)
    return fs2.task_state.need_run;
  if (fs1.task_state.idle != fs2.task_state.idle)
    return fs2.task_state.idle;
  if (fs1.task_state.skip_idle != fs2.task_state.skip_idle)
    return fs2.task_state.skip_idle;
  if (fs1.task_state.aborted != fs2.task_state.aborted)
    return fs2.task_state.aborted;
  if (fs1.task_state.finished != fs2.task_state.finished)
    return fs2.task_state.finished;
  return false;
}

std::set<FullState> states;
std::set<FullState>::iterator last_state = states.end();
std::set<std::pair<FullState, FullState>> directed_graph;
int count = 0;

void probe(int file_line, char const* description, TaskState state, int s1, char const* s1_str, int s2, char const* s2_str, int s3, char const* s3_str)
{
  static std::thread::id s_id;
  ASSERT(aithreadid::is_single_threaded(s_id));  // Fails if more than one thread executes this line.

  FullState full_state(file_line, description, state, s1, s1_str, s2, s2_str, s3, s3_str);

  // Insert the new state into the std::set.
  auto res = states.insert(full_state);
  if (res.second)
    Dout(dc::warning, description << "; " << std::hex << full_state);

  if (last_state != states.end())
  {
    auto res = directed_graph.insert(std::make_pair(*last_state, full_state));
    if (res.second)
    {
      ++count;
      Dout(dc::always, *last_state << " -> " << full_state << " {" << count << '}');
      if (count == 101)
      {
        std::ofstream ofile;
        ofile.open("transitions.dot");
        ofile << "digraph transitions {\n";
        for (auto transition : directed_graph)
          ofile << transition.first << " -> " << transition.second << ";\n";
        ofile << "}\n";
        ofile.close();
      }
    }
  }

  last_state = res.first;
}

} // namespace montecarlo
