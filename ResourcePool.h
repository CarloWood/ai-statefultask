#pragma once

#include "statefultask/AIStatefulTask.h"
#include <type_traits>
#include <deque>
#include <array>
#include <vector>

namespace statefultask {

// API to allocate batches of resources of type ResourceType.
//
// Provided an array with N ResourceType's, it is filled with new ResourceType values.
//
// An array with ResourceType's, in any order and from any batch, can be freed;
// each ResourceType in this regard is treated as an individual object.
//
class ResourceFactory
{
  // ptr_to_array_of_resources must point to an array of `size` resources,
  // the type of which is defined by the derived class.
  virtual void do_allocate(void* ptr_to_array_of_resources, size_t size) = 0;
  virtual void do_free(void const* ptr_to_array_of_resources, size_t size) = 0;

 public:
  template<typename ResourceType, size_t size>
  [[gnu::always_inline]] void allocate(std::array<ResourceType, size>& resources_out)
  {
    do_allocate(resources_out.data(), resources_out.size());
  }

  template<typename ResourceType>
  [[gnu::always_inline]] void allocate(std::vector<ResourceType>& resources_out)
  {
    do_allocate(resources_out.data(), resources_out.size());
  }

  template<typename ResourceType, size_t size>
  [[gnu::always_inline]] void free(std::array<ResourceType, size> const& resources)
  {
    do_free(resources.data(), resources.size());
  }

  template<typename ResourceType>
  [[gnu::always_inline]] void free(std::vector<ResourceType> const& resources)
  {
    do_free(resources.data(), resources.size());
  }
};

template<typename T>
concept ConceptResourceFactory = std::is_base_of_v<ResourceFactory, T> && std::is_constructible_v<typename T::resource_type>;

// A pool class is deliberately not thread-safe.
// Only a single thread at a time should call any of it's member functions.
template<ConceptResourceFactory RF>
class ResourcePool
{
 public:
  using resource_factory_type = RF;
  using resource_type = typename resource_factory_type::resource_type;
  using free_list_type = std::deque<resource_type, utils::DequeAllocator<resource_type>>;

 private:
  size_t m_max_allocations;                             // A limit on the allowed number of allocations.
  size_t m_allocations;                                 // The current number of allocations.
  size_t m_acquires;                                    // The current number of acquired resources that weren't released yet.
  resource_factory_type m_factory;                      // Factory to create and destroy resources.
  typename free_list_type::allocator_type& m_allocator_ref;  // A reference to the allocator used for m_free_list.
  free_list_type m_free_list;                           // A FILO queue for released resources.

 public:
  // The deque allocator is kept outside of the ResourcePool class so that it can be shared with other objects (it is thread-safe).
  // Any utils::DequeAllocator that allocates objects with a size equal to the size of resource_type can be used, provided it
  // has a lifetime that exceeds that of the ResourcePool.
  template<typename T, typename... Args>
  ResourcePool(size_t max_allocations, utils::DequeAllocator<T>& allocator, Args const&... factory_args) :
    m_max_allocations(max_allocations), m_allocations(0), m_acquires(0), m_factory(factory_args...), m_allocator_ref(allocator), m_free_list(allocator)
  {
    static_assert(sizeof(T) == sizeof(resource_type), "The allocation passed must allocate chunks of the right size.");
  }

  // Accessor.
  resource_factory_type const& factory() const { return m_factory; }

  void acquire(resource_type* resources, size_t size);          // Acquire resources; either from the pool or by allocating more resources.
  void release(resource_type const* resources, size_t size);    // Add resources to the pool (m_free_list).

  template<size_t size>
  [[gnu::always_inline]] void acquire(std::array<resource_type, size>& resources_out)
  {
    acquire(resources_out.data(), resources_out.size());
  }

  [[gnu::always_inline]] void acquire(std::vector<resource_type>& resources_out)
  {
    acquire(resources_out.data(), resources_out.size());
  }

  template<size_t size>
  [[gnu::always_inline]] void release(std::array<resource_type, size> const& resources)
  {
    release(resources.data(), resources.size());
  }

  [[gnu::always_inline]] void release(std::vector<resource_type> const& resources)
  {
    release(resources.data(), resources.size());
  }

  // Register a task to be notified when more resources are returned to the pool.
  // This will call task->signal(condition) when at least n resources can be allocated.
  // It is preferable that task operates in immediate-mode so that new resources are
  // handed out in the order that subscribe was called; but this also means that
  // the thread that calls release will continue to run task. It might therefore be
  // necessary to switch task to immediate mode when waiting for condition, and out
  // of it immediately after acquiring the resources.
  void subscribe(int n, AIStatefulTask const* task, AIStatefulTask::condition_type condition);
};

template<ConceptResourceFactory RF>
void ResourcePool<RF>::acquire(resource_type* resources, size_t size)
{
  m_factory.do_allocate(resources, size);
}

template<ConceptResourceFactory RF>
void ResourcePool<RF>::release(resource_type const* resources, size_t size)
{
  m_factory.do_free(resources, size);
}

} // namespace statefultask
