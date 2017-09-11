// The timer task used by AITimer

#include "sys.h"
#include "AIFrameTimer.h"

void AIFrameTimer::create(timetype interval, lambdatype callback)
{
  TimerContainer container(interval, callback);
  auto it = timer_container.begin();
  while((it != timer_container.end()) && (*it < container))
    ++it;
  timer_container.insert(it, std::move(container));
}

void AIFrameTimer::cancel()
{
  //remove timepoint from set
  //unregister callback
}

bool AIFrameTimer::isRunning()
{
  return true;//if running, return false if not running
}

void AIFrameTimer::call(std::chrono::steady_clock::time_point interval)
{
  //call all functions in the set matching the time point
}

