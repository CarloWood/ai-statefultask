// The timer task used by AITimer

#include "sys.h"
#include"AIFrameTimer.h"

void AIFrameTimer::create(std::chrono::time_point Interval, std::function<void()> callback)
{
  //add timepoint to set
  //register callback
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

void AIFrameTimer::call(std::chrono::time_point interval)
{
  //call all functions in the set matching the time point
}

