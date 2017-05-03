/**
 * @file
 * @brief Thread pool implementation.
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
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   29/04/2017
 *   - Initial version, written by Carlo Wood.
 */

#pragma once

#include "AIObjectQueue.h"
#include "debug.h"
#include "threadsafe/aithreadid.h"
#include <thread>
#include <cassert>

// Only one AIThreadPool may exist at a time; it can be accessed by
// a call to a static function AIThreadPool::instance().
//
// However, an AIThreadPool is not a singleton; it doesn't have
// private constructors and it may not be constructed before main().
// Also, it has a public move constructor.
//
// It is allowed to create an AIThreadPool and after some usage
// destruct it; and then create a new one (this is not recommended).
// The recommended usage is:
//
// int main()
// {
// #ifdef DEBUGGLOBAL
//   GlobalObjectManager::main_entered();
// #endif
//   Debug(NAMESPACE_DEBUG::init());
//
//   AIAuxiliaryThread::start();
//   AIThreadPool thread_pool;          // Creates (std::thread::hardware_concurrency() - 2) threads by default:
//                                      // the number of concurrent threads supported by the implementation
//                                      // minus one for the main thread and minus one for the auxiliary thread.
// ...
//   // Use thread_pool or AIThreadPool::instance()
//
//   AIAuxiliaryThread::stop();
// }
//
class AIThreadPool {
  private:
    static std::atomic<AIThreadPool*> s_instance;       // The only instance of AIThreadPool that should exist at a time.
    std::thread::id m_constructor_id;                   // Thread id of the thread that created and/or moved AIThreadPool.
    int m_max_number_of_threads;
    bool m_pillaged;

  public:
    AIThreadPool(int number_of_threads = std::thread::hardware_concurrency() - 2, int max_number_of_threads = std::thread::hardware_concurrency());
    AIThreadPool(AIThreadPool const&) = delete;
    AIThreadPool(AIThreadPool&& rvalue) : m_max_number_of_threads(rvalue.m_max_number_of_threads), m_pillaged(false)
    {
      // The move constructor is not thread-safe. Only the thread that constructed us may move us.
      assert(aithreadid::is_single_threaded(m_constructor_id));
      rvalue.m_pillaged = true;
      // Once we're done with constructing this object, other threads (that likely still have to be started,
      // but that is not enforced) should be allowed to call AIThreadPool::instance(). In order to enforce
      // that all initialization of this object will be visible to those other threads, we need to prohibit
      // that stores done before this point arrive in such threads reordered to after this store.
      s_instance.store(this, std::memory_order_release);
    }
    ~AIThreadPool();

    void change_number_of_threads_to(int number_of_threads);

    static AIThreadPool& instance()
    {
      // Construct an AIThreadPool somewhere, preferably at the beginning of main().
      ASSERT(s_instance != nullptr);
      // In order to see the full construction of the AIThreadPool instance, we need to prohibit
      // reads done after this point from being reordered before this load, because that could
      // potentially still read memory locations in the object from before when it was constructed.
      return *s_instance.load(std::memory_order_acquire);
    }
};

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
extern channel_ct threadpool;
NAMESPACE_DEBUG_CHANNELS_END
#endif
