/**
 * @file
 * @brief Declaration of AIAuxiliaryThread.
 *
 * Copyright (C) 2017  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "utils/Singleton.h"
#include "threadsafe/aithreadsafe.h"

/*!
 * @brief A singleton interface to start and stop \ref gAuxiliaryThreadEngine.
 *
 * The \c gAuxiliaryThreadEngine must be started early, and stopped before cleanup destruction,
 * by calling the <em>static</em> \ref start and \ref stop member functions of this class.
 *
 * The real AIEngine (\c gAuxiliaryThreadEngine) is hidden from the user:
 * <em>this</em> object is not an \c %AIEngine (most notably, there is no need to call
 * \c gAuxiliaryThreadEngine.mainloop() in a loop somewhere).
 *
 * @sa helloworld_example The HelloWorld example mentioned on the main page.
 */
class AIAuxiliaryThread : public Singleton<AIAuxiliaryThread>
{
  friend_Instance;
 private:
  // MAIN-THREAD
  AIAuxiliaryThread() : m_keep_running(false), m_stopped(true) { }
  ~AIAuxiliaryThread() { }
  AIAuxiliaryThread(AIAuxiliaryThread const&) : Singleton<AIAuxiliaryThread>() { }

 private:
  std::thread m_handle;
  using keep_running_type = aithreadsafe::Wrapper<bool, aithreadsafe::policy::Primitive<std::mutex>>;
  keep_running_type m_keep_running;
  using stopped_type = aithreadsafe::Wrapper<bool, aithreadsafe::policy::Primitive<std::mutex>>;
  stopped_type m_stopped;

 public:
  /*!
   * @brief Start the auxiliary thread.
   *
   * Call this from \c main() before any task is \c run.
   */
  static void start();

  /*!
   * @brief Stop the auxiliary thread.
   *
   * Call this before destructing objects that might be used by (call backs of)
   * still running tasks for a clean destruction (avoiding running tasks doing
   * call backs of destructed objects).
   *
   * After this no new task may be (re)started anymore.
   */
  static void stop();

 private:
  static void mainloop();
};
