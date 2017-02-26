#include "sys.h"
#include "montecarlo_test.h"
#include "AIStatefulTask.h"
#include "threadsafe/aithreadid.h"
#include "debug.h"
#include <map>
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

  bool collapses(FullState const& fs) const { return task_state.equivalent(fs.task_state); }

  void print_s123(std::ostream& os) const;
  void print_base_and_avdance_state(std::ostream& os) const;
  void print_task_state(std::ostream& os) const;
};

std::ostream& operator<<(std::ostream& os, FullState const& full_state)
{
  os << "{ '" << full_state.description << "' (" << std::dec << full_state.line << ")";
  full_state.print_s123(os);
  os << ", ";
  full_state.print_base_and_avdance_state(os);
  os << ", ";
  full_state.print_task_state(os);
  return os << '}';
}

void FullState::print_s123(std::ostream& os) const
{
  if (s1 != -1) os << ' ' << s1_str;
  if (s2 != -1) os << '/' << s2_str;
  if (s3 != -1) os << '/' << s3_str;
}

void FullState::print_base_and_avdance_state(std::ostream& os) const
{
  os << task_state.base_state_str << '/' << task_state.run_state_str;
  if (task_state.advance_state) os << '/' << task_state.advance_state_str;
}

void FullState::print_task_state(std::ostream& os) const
{
  if (task_state.need_run) os << " need_run";
  if (task_state.idle) os << " idle";
  if (task_state.skip_idle) os << " skip_idle";
  if (task_state.blocked) os << " blocked";
  if (task_state.reset) os << " reset";
  if (task_state.aborted) os << " aborted";
  if (task_state.finished) os << " finished";
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

struct Data {
  std::string name;
  int inputs;
  int outputs;

  Data(std::string const& _name) : name(_name), inputs(0), outputs(0) { }
  Data() { ASSERT(false); }
};

std::ostream& operator<<(std::ostream& os, Data const& data)
{
  return os << data.name;
}

std::map<FullState, Data> states;
std::map<FullState, Data>::iterator last_state = states.end();
std::set<std::pair<FullState, FullState>> directed_graph;
int count = 0;

void write_transitions_gv();

void probe(int file_line, char const* description, TaskState state, int s1, char const* s1_str, int s2, char const* s2_str, int s3, char const* s3_str)
{
  static std::thread::id s_id;
  ASSERT(aithreadid::is_single_threaded(s_id));  // Fails if more than one thread executes this line.

  FullState full_state(file_line, description, state, s1, s1_str, s2, s2_str, s3, s3_str);

  // Insert the new state into the std::set.
  auto it = states.find(full_state);
  if (it == states.end())
  {
    static int node_count = 0;
    std::stringstream node_name;
    node_name << "n" << node_count;
    ++node_count;
    Dout(dc::warning, "New node (" << node_name.str() << "): " << full_state);
    auto res = states.insert(std::make_pair(full_state, node_name.str()));
    it = res.first;
  }

  if (last_state != states.end())
  {
    auto res = directed_graph.insert(std::make_pair(last_state->first, full_state));
    if (res.second)
    {
      ++count;
      Dout(dc::always, last_state->first << "(" << last_state->second << ") -> " << full_state << " {" << count << '}');
      last_state->second.outputs++;
      it->second.inputs++;
      if (count == 101)
        write_transitions_gv();
    }
  }

  last_state = it;
}

struct Node {
  std::map<FullState, Data>::iterator me;
  std::list<std::list<Node>::iterator> inputs;
  std::list<std::list<Node>::iterator> outputs;

  Node(std::map<FullState, Data>::iterator _me) : me(_me) { }

  bool operator==(std::map<FullState, Data>::iterator const& _me) const { return me == _me; }
  bool single_inout() const { return inputs.size() == 1 && outputs.size() == 1; }
  bool collapses() const { return single_inout() && (*outputs.begin())->single_inout() && me->first.collapses((*outputs.begin())->me->first); }
};

std::ostream& operator<<(std::ostream& os, std::list<Node>::iterator const& node)
{
  auto begin_node = node;
  for (;;)
  {
    auto tmp = begin_node->inputs.begin();
    if (tmp == begin_node->inputs.end() || !(*tmp)->collapses())
      break;
    begin_node = *tmp;
  }
  auto end_node = node;
  while (end_node->collapses())
    end_node = *end_node->outputs.begin();
  std::string name = begin_node->me->second.name;
  if (begin_node != end_node)
    name += '_' + end_node->me->second.name;
  return os << name;
}

std::list<Node> nodes;

void write_transitions_gv()
{
  // First convert directed_graph to something more managable.
  for (auto transition : directed_graph)
  {
    auto from = states.find(transition.first);
    auto to = states.find(transition.second);
    auto from_node = std::find(nodes.begin(), nodes.end(), from);
    if (from_node == nodes.end())
    {
      nodes.push_back(from);
      from_node = nodes.end();
      --from_node;
    }
    auto to_node = std::find(nodes.begin(), nodes.end(), to);
    if (to_node == nodes.end())
    {
      nodes.push_back(to);
      to_node = nodes.end();
      --to_node;
    }
    from_node->outputs.push_back(to_node);
    to_node->inputs.push_back(from_node);
  }

  std::ofstream ofile;
  ofile.open("transitions.gv");
  ofile << "strict digraph transitions {\n";
  ofile << "  node [style=filled];\n";
  for (auto node = nodes.begin(); node != nodes.end(); ++node)
  {
    if (node->collapses())
      continue;
    ofile << "  " << node << "[";
    // Print node label.
    ofile << "label=\"" << node->me->first.description << " (#" << node->me->first.line << ")\n";
    FullState const& fs(node->me->first);
    fs.print_s123(ofile);
    ofile << "\n";
    fs.print_base_and_avdance_state(ofile);
    ofile << "\n";
    fs.print_task_state(ofile);
    ofile << "\"";

    TaskState const& ts(fs.task_state);
    if (ts.idle)
      ofile << ",color=green";
    else if (ts.skip_idle)
      ofile << ",color=lightblue";
    if (ts.run_state == AIStatefulTask::max_state)
      ofile << ",shape=box";
    else if (ts.run_state == AIStatefulTask::max_state + 1)
      ofile << ",shape=hexagon";
    ofile << "];\n";
    for (auto out : node->outputs)
      ofile << "  " << node << " -> " << out << ";\n";
  }
  ofile << "}\n";
  ofile.close();
}

} // namespace montecarlo
