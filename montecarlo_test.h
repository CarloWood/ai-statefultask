#pragma once

#include <cstddef>      // size_t

class AIStatefulTask;

namespace montecarlo {

int const AIStatefulTask_cxx = 0;
int const AIStatefulTask_h = 10000;

void probe(int line, char const* description, size_t hash, int s1 = -1, int s2 = -1, int s3 = -1);

} // namespace montecarlo
