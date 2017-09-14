// The timer task used by AITimer

#include "sys.h"
#include "AIFrameTimer.h"

void AIFrameTimer::create(timetype const& interval, lambdatype const& callback)
{
  auto result = timer_containers.emplace(interval, callback);
  if(!result.second)
    result.first->push_back(callback);
}

void AIFrameTimer::cancel(timetype const& interval, lambdatype const& callback)
{
  auto result = timer_containers.find(interval);
  if(result != timer_containers.end())
    if(result->remove(callback))
      timer_containers.erase(result);    
}

bool AIFrameTimer::isRunning(timetype const& interval, lambdatype const& callback)
{
  return timer_containers.find(interval)->is_in(callback);
}

void AIFrameTimer::expire(timetype const& interval)
{
  auto result = timer_containers.find(interval);
  if(result == timer_containers.end())
    return;
  result->call();
  timer_containers.erase(result);
}

