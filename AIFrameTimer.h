#pragma once

// The timer task used in AITimer.

#include<vector>
#include<chrono>
#include<memory>

using timetype = std::chrono::steady_clock::time_point;
using lambdatype = std::function<void()>;

// Container class for timed callbacks

class TimerContainer {
  private:
    timetype const m_time;
    std::vector<lambdatype> m_callbacks;

  public:
    TimerContainer(timetype const& time, lambdatype callback) : m_time(time), m_callbacks(1, callback) {}
    void push_back(lambdatype call) {m_callbacks.push_back(call);}
    std::vector<lambdatype> eject() {return m_callbacks;}

    friend bool operator<(TimerContainer const& lhs, TimerContainer const& rhs) {return lhs.m_time < rhs.m_time;}
    TimerContainer(TimerContainer&& timer_container) : m_time(std::move(timer_container.m_time)), m_callbacks(std::move(timer_container.m_callbacks)) { }
};


// The timer class used in AITimer.

class AIFrameTimer {
  private:
    std::vector<TimerContainer> timer_container;

  public:
    AIFrameTimer() {}
    void create(timetype interval, lambdatype callback);
    void cancel();
    bool isRunning();

  private:
    void call(std::chrono::steady_clock::time_point Interval);
};

