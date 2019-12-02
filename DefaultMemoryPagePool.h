/**
 * ai-statefultask -- Asynchronous, Stateful Task Scheduler library.
 *
 * @file
 * @brief Declaration of class DefaultMemoryPagePool.
 *
 * @Copyright (C) 2019  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This file is part of ai-statefultask.
 *
 * Ai-statefultask is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Ai-statefultask is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with ai-statefultask.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "utils/MemoryPagePool.h"
#include "AIStatefulTaskMutex.h"

namespace statefultask {

/// Base class of DefaultMemoryPagePool.
class DefaultMemoryPagePoolBase
{
 private:
  static utils::MemoryPagePool* s_instance;

 protected:
  static void init(utils::MemoryPagePool* mpp)
  {
    // Only create one DefaultMemoryPagePool object (at the top of main()).
    ASSERT(s_instance == nullptr);
    s_instance = mpp;
    AIStatefulTaskMutex::init(mpp);
  }

  DefaultMemoryPagePoolBase() = default;
  ~DefaultMemoryPagePoolBase()
  {
    delete s_instance;
    s_instance = nullptr;
  }

 public:
  /**
   * Returns a reference to the default @c{utils::MemoryPagePool}.
   *
   * This is the singleton that is initialized at the top of @c{main()}.
   * @sa DefaultMemoryPagePool
   */
  static utils::MemoryPagePool& instance()
  {
    // Create a statefultask::DefaultMemoryPagePool at the top of main,
    // and/or don't use anything from statefultask in constructors/destructors
    // of global and/or static objects.
    ASSERT(s_instance);
    return *s_instance;
  }
};

/**
 * The memory pool for all other memory pools.
 *
 * DefaultMemoryPagePool is a globally accessible MemoryPagePool singleton,
 * but with a life time equal of that of @c{main()}. The `Default`-part refers
 * to the fact that most memory pools, if not all, of a process can use
 * this singleton as their MemoryPagePool. Unless you know what you are doing
 * you're advised to use the default constructor and have all memory pools
 * that are based on an @c{utils::MemoryPagePool} use
 * @link statefultask::DefaultMemoryPagePoolBase::instance AIMemoryPagePool::instance() @endlink.
 *
 * DefaultMemoryPagePool must be initialized at the start of main before
 * using anything else from statefultask.
 *
 * Recommended usage,
 *
 * @code
 * int main()
 * {
 *   Debug(NAMESPACE_DEBUG::init();
 *
 *   AIMemoryPagePool mpp;             // Add this line at the top of main.
 *
 *   ...
 * }
 * @endcode
 *
 * The above line is equivalent to one of
 *
 * @code
 * statefultask::DefaultMemoryPagePool mpp{};
 *     // The curly braces are required,
 *
 * statefultask::DefaultMemoryPagePool mpp(0x8000);
 *     // unless you pass at least one argument,
 *
 * statefultask::DefaultMemoryPagePool<utils::MemoryPagePool> mpp;
 *     // or specify the (default) template parameter explicitly.
 * @endcode
 *
 * In all cases AIMemoryPagePool::instance() becomes usable,
 * which is currently only required when using AIStatefulTaskMutex (hence anything
 * from filelock-task).
 *
 * Fine tuning is possible by passing the constructor arguments of a @c{MemoryPagePool}
 * to DefaultMemoryPagePool, or to derive your own class from @c{MemoryPagePool} and pass
 * that to DefaultMemoryPagePool as template argument. For example,
 *
 * @code
 * statefultask::DefaultMemoryPagePool<MyMPP>
 *     default_memory_page_pool(constructor arguments of MyMPP);
 * @endcode
 *
 * where `MyMPP` must be derived from @c{utils::MemoryPagePool}.
 */
template<typename MPP = utils::MemoryPagePool>
class DefaultMemoryPagePool : public DefaultMemoryPagePoolBase
{
 public:
  /// Constructor with the same (default) arguments as @c{utils::MemoryPagePool}.
  DefaultMemoryPagePool(size_t block_size = 0x8000, utils::MemoryPagePool::blocks_t minimum_chunk_size = 0, utils::MemoryPagePool::blocks_t maximum_chunk_size = 0)
  {
    init(new MPP(block_size, minimum_chunk_size, maximum_chunk_size));
  }

  /**
   * Constructor to be used for non-default template parameter @tt{MPP} when that
   * class takes different arguments than @c{utils::MemoryPagePool}.
   *
   * Note that @c{MPP} still must be derived from @c{utils::MemoryPagePool}.
   */
  template<typename... ArgT>
  DefaultMemoryPagePool(ArgT&&... args)
  {
    init(new MPP(std::forward<ArgT>(args)...));
  }
};

} // namespace statefultask

/**
 * Short version of `statefultask::DefaultMemoryPagePool<utils::MemoryPagePool>`.
 *
 * See statefultask::DefaultMemoryPagePool for a description of the constructor.
 */
using AIMemoryPagePool = statefultask::DefaultMemoryPagePool<utils::MemoryPagePool>;
