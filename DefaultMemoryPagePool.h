#include "utils/MemoryPagePool.h"
#include "AIStatefulTaskMutex.h"

namespace statefultask {

// DefaultMemoryPagePool is a globally accessible MemoryPagePool singleton,
// but with a life time equal of that of main().
//
// It must be initialized at the start of main before using of anything else
// from statefultask.
//
// For example,
//
// int main()
// {
//   Debug(NAMESPACE_DEBUG::init();
//
//   AIMemoryPagePool mpp;                                              // Add this line at the top of main.
//
//   ...
// }
//
// The above line is equivalent to one of
//
//   statefultask::DefaultMemoryPagePool mpp{};                         // The curly braces are required...
//   statefultask::DefaultMemoryPagePool mpp(0x8000);                   // ...unless you pass at least one argument.
//   statefultask::DefaultMemoryPagePool<utils::MemoryPagePool> mpp;    // Or specify the (default) template parameter explicitly.
//
// In all cases AIMemoryPagePool::instance() becomes usable,
// which is currently only required when using AIStatefulTaskMutex (hence anything
// from filelock-task).
//
// Fine tuning is possible by passing the constructor arguments of a MemoryPagePool
// to DefaultMemoryPagePool, or to derive your own class from MemoryPagePool and pass
// that to DefaultMemoryPagePool as template argument. For example,
//
//   statefultask::DefaultMemoryPagePool<MyMPP> default_memory_page_pool(constructor arguments of MyMPP);
//
// where MyMPP must be derived from utils::MemoryPagePool.
//
class DefaultMemoryPagePoolBase
{
 private:
  static utils::MemoryPagePool* s_instance;

 protected:
  static void init(utils::MemoryPagePool* mpp)
  {
    // Only create a one DefaultMemoryPagePool object (at the top of main()).
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
  static utils::MemoryPagePool& instance()
  {
    // Create a statefultask::DefaultMemoryPagePool at the top of main,
    // and/or don't use anything from statefultask in constructors/destructors
    // of global and/or static objects.
    ASSERT(s_instance);
    return *s_instance;
  }
};

template<typename MPP = utils::MemoryPagePool>
class DefaultMemoryPagePool : public DefaultMemoryPagePoolBase
{
 public:
  DefaultMemoryPagePool(size_t block_size = 0x8000, utils::MemoryPagePool::blocks_t minimum_chunk_size = 0, utils::MemoryPagePool::blocks_t maximum_chunk_size = 0)
  {
    init(new MPP(block_size, minimum_chunk_size, maximum_chunk_size));
  }

  template<typename... ArgT>
  DefaultMemoryPagePool(ArgT&&... args)
  {
    init(new MPP(std::forward<ArgT>(args)...));
  }
};

} // namespace statefultask

using AIMemoryPagePool = statefultask::DefaultMemoryPagePool<utils::MemoryPagePool>;
