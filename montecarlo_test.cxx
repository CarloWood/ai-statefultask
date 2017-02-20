#include "sys.h"
#include "montecarlo_test.h"
#include "AIStatefulTask.h"
#include <boost/functional/hash.hpp>
#include "debug.h"

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
channel_ct montecarlo("MONTECARLO");
NAMESPACE_DEBUG_CHANNELS_END
#endif

namespace montecarlo {

std::set<size_t> states;

void probe(int file_line, char const* description, size_t hash, int s1, int s2, int s3)
{
  // Calculate the hash of the current state.
  boost::hash_combine(hash, file_line);
  boost::hash_combine(hash, s1);
  boost::hash_combine(hash, s2);
  boost::hash_combine(hash, s3);

  Dout(dc::montecarlo, description << "; " << std::hex << hash);
}

} // namespace montecarlo
