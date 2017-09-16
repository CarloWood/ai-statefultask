#pragma once

// The timer task used in AITimer.

#include<vector>
#include<set>
#include<chrono>
#include<memory>

using timetype = std::chrono::steady_clock::time_point;
using lambdatype = std::function<void()>;

// Container class for timed callbacks

class TimerContainer {
  private:
    timetype m_time;
    std::vector<lambdatype> m_callbacks;

  public:
    TimerContainer(timetype const& time, lambdatype const& callback) : m_time(time), m_callbacks(1, callback) { }
    //TimerContainer(TimerContainer const&& other) : m_time(std::move(other.m_time)), m_callbacks(std::move(other.m_callbacks)) {}
    void push_back(lambdatype const& callback) {m_callbacks.push_back(callback);}
    void call() {for(lambdatype callback : m_callbacks) callback();}
    bool is_in(lambdatype const& callback) {for(lambdatype in_vector : m_callbacks) if(callback == in_vector) return true;}
    bool remove(lambdatype const& callback)
    {
      std::erase(
          std::remove_if(m_callbacks.begin(), m_callbacks.end(), [&callback](lambdatype const& cb){ return callback == cb}),
          m_callbacks.end()
      );
      return m_callbacks.empty();
    }

    friend bool operator<(TimerContainer const& lhs, TimerContainer const& rhs) {return lhs.m_time < rhs.m_time;}
    friend bool operator==(TimerContainer const& lhs, timetype const& rhs) {return lhs.m_time < rhs;}
    //TimerContainer& operator=(TimerContainer const& other) {return TimerContainer(other);}
};


// The timer class used in AITimer.

class AIFrameTimer {
  private:
    std::set<TimerContainer> timer_containers;

  public:
    AIFrameTimer() {}
    void create(timetype const& interval, lambdatype const& callback);
    void cancel(timetype const& interval, lambdatype const& callback);
    bool isRunning(timetype const& interval, lambdatype const& callback);

  private:
    void expire(timetype const& Interval);
};

