#pragma once

#include "statefultask/AIStatefulTask.h"
#include <type_traits>
#include <deque>
#include <array>
#include <vector>
#include <map>

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

  struct EventRequest
  {
    AIStatefulTask* m_task;                             // Call m_task->signal(m_condition) when m_number_of_needed_resources are available.
    int m_number_of_needed_resources;
    AIStatefulTask::condition_type m_condition;

    EventRequest(AIStatefulTask* task, int number_of_needed_resources, AIStatefulTask::condition_type condition) :
      m_task(task), m_number_of_needed_resources(number_of_needed_resources), m_condition(condition) { }
  };
  using event_requests_container_type = std::vector<EventRequest>;
  using event_requests_type = threadsafe::Unlocked<event_requests_container_type, threadsafe::policy::Primitive<std::mutex>>;

 private:
  size_t m_max_allocations;                             // A limit on the allowed number of allocations.
  size_t m_allocations;                                 // The current number of allocations.
  size_t m_acquires;                                    // The current number of acquired resources that weren't released yet.
  resource_factory_type m_factory;                      // Factory to create and destroy resources.
  typename free_list_type::allocator_type& m_allocator_ref;  // A reference to the allocator used for m_free_list.
  free_list_type m_free_list;                           // A FILO queue for released resources.
  event_requests_type m_event_requests;                 // A list of tasks that want to be woken up when new resources are available.

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

  // Acquire resources; either from the pool or by allocating more resources.
  // Returns the number of actually acquired resources. It can be less than size when m_max_allocations is reached.
  [[nodiscard]] size_t acquire(resource_type* resources, size_t const size);

  // Add resources to the pool (m_free_list).
  void release(resource_type const* resources, size_t size);

  template<size_t size>
  [[nodiscard, gnu::always_inline]] size_t acquire(std::array<resource_type, size>& resources_out)
  {
    return acquire(resources_out.data(), resources_out.size());
  }

  [[nodiscard, gnu::always_inline]] size_t acquire(std::vector<resource_type>& resources_out)
  {
    return acquire(resources_out.data(), resources_out.size());
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
  void subscribe(int n, AIStatefulTask* task, AIStatefulTask::condition_type condition);
};

template<ConceptResourceFactory RF>
size_t ResourcePool<RF>::acquire(resource_type* resources, size_t const size)
{
  DoutEntering(dc::notice|continued_cf, "ResourcePool<" << type_info_of<RF>().demangled_name() << ">::acquire(resources (" << resources << "), " << size << ") = ");
  // The number of already allocated, free resources.
  size_t const in_pool = m_free_list.size();
  // The index into resources[] that must be filled next.
  size_t index = 0;
  // First get resources from the pool, if any.
  if (index < in_pool)
  {
    auto resource = m_free_list.begin();
    // Get size resources from the free list, or however many are in there.
    while (index < std::min(in_pool, size))
    {
      Dout(dc::notice, "resources[" << index << "] = " << *resource << " (from m_free_list)");
      resources[index++] = *resource++;
    }
    // Erase the used resources from the free list.
    m_free_list.erase(m_free_list.begin(), resource);
  }
  // Get the remaining resources from m_factory, if any.
  if (index < size)
  {
    // We are not allowed to allocate more than m_max_allocations resources in total.
    // This line allows for m_allocations to be larger than m_max_allocations (causing to_allocate
    // to become zero) in case of future dynamic adjustments of m_max_allocations.
    size_t to_allocate = std::min(size - index, std::max(m_max_allocations, m_allocations) - m_allocations);
    if (to_allocate > 0)
    {
      m_factory.do_allocate(&resources[index], to_allocate);
#ifdef CWDEBUG
      for (int j = 0; j < to_allocate; ++j)
        Dout(dc::notice, "resources[" << (index + j) << "] = " << resources[index + j] << " (from m_factory)");
#endif
      index += to_allocate;
      m_allocations += to_allocate;
    }
  }
  // Return the number of actually acquired resources.
  Dout(dc::finish, index);
  return index;
}

template<ConceptResourceFactory RF>
void ResourcePool<RF>::release(resource_type const* resources, size_t size)
{
  DoutEntering(dc::notice, "ResourcePool<" << type_info_of<RF>().demangled_name() << ">::release(resources (" << resources << "), " << size << ")");
  // In case of future dynamic adjustments of m_max_allocations.
  if (AI_UNLIKELY(m_allocations > m_max_allocations))
  {
    // Free some or all of the resources immediately.
    size_t to_free = std::min(size, m_allocations - m_max_allocations);
    if (to_free > 0)    // Might be zero when size is zero for some reason. No need to assert in that case.
    {
#ifdef CWDEBUG
      for (int j = 0; j < to_free; ++j)
        Dout(dc::notice, resources[j] << " is returned to m_factory.");
#endif
      m_factory.do_free(resources, to_free);
    }
    m_allocations -= to_free;
    size -= to_free;
    resources += to_free;
  }
  // Put `size` resources on the free list.
  size_t index = 0;
  while (index < size)
  {
    Dout(dc::notice, resources[index] << " put back on m_free_list.");
    m_free_list.push_back(resources[index++]);
  }
  // Notify waiting tasks.
  std::vector<EventRequest> must_be_notified;
  {
    typename event_requests_type::wat event_requests_w(m_event_requests);
    if (AI_LIKELY(event_requests_w->empty()))
      return;
    size_t available_resources = m_max_allocations - m_allocations + m_free_list.size();
    // Copy elements from event_request to must_be_notified until we ran out of available_resources.
    typename event_requests_container_type::iterator event_request = event_requests_w->begin();
    while (event_request != event_requests_w->end())
    {
      if (event_request->m_number_of_needed_resources > available_resources)
        break;
      must_be_notified.push_back(*event_request);
      available_resources -= event_request->m_number_of_needed_resources;
      ++event_request;
    }
    // Remove the copied elements.
    event_requests_w->erase(event_requests_w->begin(), event_request);
  }
  // While m_event_requests is unlocked, notify the lucky tasks.
  for (auto const& event_request : must_be_notified)
    event_request.m_task->signal(event_request.m_condition);
}

template<ConceptResourceFactory RF>
void ResourcePool<RF>::subscribe(int n, AIStatefulTask* task, AIStatefulTask::condition_type condition)
{
  typename event_requests_type::wat event_requests_w(m_event_requests);
  typename event_requests_container_type::iterator iter = event_requests_w->begin();
  // The same task can call this "spuriously". Lets just remember the largest n (although if that happens n should be the same).
  while (iter != event_requests_w->end())
    if (iter->m_task == task)
    {
      if (AI_UNLIKELY(n > iter->m_number_of_needed_resources))
        iter->m_number_of_needed_resources = n;
      return;
    }
  event_requests_w->emplace_back(task, n, condition);
}

} // namespace statefultask
