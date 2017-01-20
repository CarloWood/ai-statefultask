/**
 * @file
 * @brief Implementation of AIAuxiliaryThread.
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

#include "sys.h"
#include "AIAuxiliaryThread.h"
#include "AIEngine.h"

namespace {
SingletonInstance<AIAuxiliaryThread> dummy __attribute__ ((__unused__));
}

void AIAuxiliaryThread::mainloop(void)
{
  AIAuxiliaryThread& auxiliary_thread(instance());
  while(*keep_running_type::crat(auxiliary_thread.m_keep_running))
  {
    gAuxiliaryThreadEngine.threadloop();
  }
  *stopped_type::wat(auxiliary_thread.m_stopped) = true;
}

void AIAuxiliaryThread::start(void)
{
  AIAuxiliaryThread& auxiliary_thread(instance());
  {
    stopped_type::wat stopped_w(auxiliary_thread.m_stopped);
    if (!*stopped_w)
      return;
    *stopped_w = false;
  }
  *keep_running_type::wat(auxiliary_thread.m_keep_running) = true;
  std::thread new_thread_handle(mainloop);
  auxiliary_thread.m_handle.swap(new_thread_handle);
}

void AIAuxiliaryThread::stop(void)
{
  AIAuxiliaryThread& auxiliary_thread(instance());
  {
    keep_running_type::wat keep_running_w(auxiliary_thread.m_keep_running);
    if (!*keep_running_w)      // Already stopped?
      return;
    *keep_running_w = false;
  }
  gAuxiliaryThreadEngine.wake_up();
  bool stopped;
  int count = 401;
  while(!(stopped = *stopped_type::crat(auxiliary_thread.m_stopped)) && --count)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (stopped)
    auxiliary_thread.m_handle.join();
  else
    auxiliary_thread.m_handle.detach();
  Dout(dc::notice, "Stateful task thread" << (!stopped ? " not" : "") << " stopped after " << ((400 - count) * 10) << "ms.");
}
