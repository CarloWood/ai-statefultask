#pragma once

#include<chrono>

// The timer task used in AITimer.

class AIFrameTimer {
  private:
    //list of time points bound to callbacks

  public:
    AIFrameTimer() {}
    void create(std::chrono::steady_clock::time_point interval, std::function<void()> callback);
    void cancel();
    bool isRunning();

  private:
    void call(std::chrono::steady_clock::time_point Interval);
};

