/**
 * @file
 * @brief A FIFO buffer for moveable objects.
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
 *   27/04/2017
 *   - Initial version, written by Carlo Wood.
 */

#pragma once

#include "utils/macros.h"
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdlib>
#include <cstdint>

// A ring buffer for moveable objects.
//
// Usage (using for example std::function<void()>):
//
// constexpr int capacity = 8;
// AIObjectQueue<std::function<void()>> queue(capacity);    // Allocates 9 std::function<void()> objects.
//
// The actual size of the allocated memory for this
// queue will be nine: eight object can be written
// to the queue without reading anything from it.
// The space for the nineth object is reserved to
// store the last object that was already read from
// the queue but wasn't popped yet.
//
// // Producer threads:
// { // Lock the queue for other producer threads.
//   auto access = queue.producer_access();
//   int length = access.length();
//   if (length == capacity) { /* Buffer full */ return; }
//   access.move_in([](){ std::cout << "Hello\n"; });
// } // Unlock the queue.
//
// The value of `length' allows one to do client-side
// bandwidth control; by reducing the throughput till
// the returned length is as small as possible one can
// reduce the latency.
//
// Note that due to race conditions, the actual length
// might be less, as consumer threads can read from the
// queue while we're hold the producer lock.
//
// // Consumer threads:
// std::function<void()> f;
// { // Lock the queue for other consumer threads.
//   auto access = queue.consumer_access();
//   int length = access.length();
//   if (length == 0) { /* Buffer empty */ return; }
//   f = access.move_out();
// } // Unlock the queue.
// f(); // Invoke the functor.
//
// Here the actual length might be greater than the
// returned value because producer threads can still
// write to the queue while we hold the consumer lock.
// In fact, the call to length() is in both cases the
// same function (which doesn't require locking at all
// to be thread-safe) but the locking by accessing
// length() through the respective access objects is
// necessary to prevent a producer thread having a
// race condition with another producer thread, or
// a consumer thread having a race condition with another
// consumer thread.

template<typename T>
class AIObjectQueue {
  using size_type = std::uint_fast32_t;   // "4294967295 objects ought to be enough for anybody." --Bill Gates.

 private:
  T* m_start;                       // Start of buffer.
  size_type m_capacity;             // Number of objects of type T in the buffer.
  std::atomic<size_type> m_head;    // Next write position.
  std::atomic<size_type> m_tail;    // Next read position.
  std::mutex m_producer_mutex;
  std::mutex m_consumer_mutex;

 public:
  static constexpr size_t alignment = (alignof(T) < 32) ? (size_t)32 : alignof(T);

 public:
  AIObjectQueue() : m_start(nullptr), m_capacity(0), m_head(0), m_tail(0) { }
  AIObjectQueue(int objects) : m_start(nullptr), m_capacity(0), m_head(0), m_tail(0) { allocate_(objects); }
  ~AIObjectQueue() { if (m_capacity) deallocate_(); }

  int capacity(void) const { return m_capacity; }

 private:
  void allocate_(int objects)
  {
    // Whatever...
    if (AI_UNLIKELY(objects <= 0))
    {
      Dout(dc::warning, "Calling AIObjectQueue::allocate_(" << objects << ")");
      // Bring object to state of default constructed.
      ASSERT(m_start == nullptr && m_capacity == 0);
      m_head = m_tail = 0;
      return;
    }

    // Allocate storage aligned to at least 32 bytes.
    void* storage;
    // Allocate one object more than requested.
    int ret = posix_memalign(&storage, alignment, (objects + 1) * sizeof(T));
    Dout(dc::malloc, "storage = " << storage);
    if (ret != 0)
    {
      Dout(dc::warning, "posix_memalign(" << &storage << ", " << alignment << ", " << (objects + 1) * sizeof(T) << ") returned " << ret);
      throw std::bad_alloc();
    }
    m_start = static_cast<T*>(storage);
    Dout(dc::malloc, "m_start = " << (void*)m_start);
    m_capacity = objects;
    for (size_t i = 0; i <= m_capacity; ++i)
      new (&m_start[i]) T;
    // Clean
    m_tail = 0;
    m_head = 0;
  }

  void deallocate_()
  {
    T* object = m_start;
    for (size_t i = 0; i <= m_capacity; ++i)
      object[i].~T();
    free(m_start);
    m_capacity = 0;
    m_start = nullptr;
  }

  // Return index advanced one chunk.
  size_t increment(size_type index) const
  {
    // This is branchless and much faster than using modulo.
    return index == m_capacity ? 0 : index + 1;
  }

  //-------------------------------------------------------------------------
  // Producer thread.
  // These member functions are accessed through ProducerAccess.

  int producer_length() const
  {
    auto const current_head = m_head.load(std::memory_order_relaxed);
    auto const current_tail = m_tail.load(std::memory_order_acquire);
    // If increment(current_head) == current_tail then the queue is full and we should return m_capacity.
    int length = current_head - current_tail;
    return length >= 0 ? length : length + m_capacity + 1;
  }

  void move_in(T&& object)
  {
    auto const current_head = m_head.load(std::memory_order_relaxed);
    auto const next_head    = increment(current_head);

    // Call and test ProducerAccess::length() (must be less than the capacity that this
    // AIObjectQueue was constructed with (m_capacity)) before calling move_in().
    ASSERT(next_head != m_tail.load(std::memory_order_acquire));
    // Call reallocate() after a default construction, or pass the size of the queue (in objects) when constructing it.
    ASSERT(m_capacity > 0);

    m_start[current_head] = std::move(object);
    m_head.store(next_head, std::memory_order_release);
  }

  //-------------------------------------------------------------------------
  // Consumer thread.
  // These member functions are accessed through ConsumerAccess.

  int consumer_length() const
  {
    auto const current_tail = m_tail.load(std::memory_order_relaxed);
    auto const current_head = m_head.load(std::memory_order_acquire);
    // If current_tail == current_head then the queue is empty and we should return 0.
    int length = current_head - current_tail;
    return length >= 0 ? length : length + m_capacity + 1;
  }

  // This function should only be called when consumer_length() returned a value
  // larger than zero while holding the consumer lock (hence, m_tail didn't change
  // in the meantime).
  T move_out()
  {
    auto const current_tail = m_tail.load(std::memory_order_relaxed);

    // Call and test ConsumerAccess::length() (must be larger than zero) before calling move_out().
    ASSERT(current_tail != m_head.load(std::memory_order_acquire));

    auto const next_tail = increment(current_tail);
    m_tail.store(next_tail, std::memory_order_release);
    return std::move(m_start[current_tail]);
  }

  //-------------------------------------------------------------------------

 public:
  void reallocate(int objects)
  {
    // Buffer may not be in use (these locks also protect access to the buffer itself).
    std::unique_lock<std::mutex> lock1(m_producer_mutex);
    std::unique_lock<std::mutex> lock2(m_consumer_mutex);
    // Allow reallocation.
    if (m_capacity) deallocate_();
    allocate_(objects);
  }

  struct ProducerAccess {
   private:
    AIObjectQueue<T>* m_buffer;
   public:
    ProducerAccess(AIObjectQueue<T>* buffer) : m_buffer(buffer) { buffer->m_producer_mutex.lock(); }
    ~ProducerAccess() { m_buffer->m_producer_mutex.unlock(); }
    int length() const { return m_buffer->producer_length(); }
    void move_in(T&& ptr) { m_buffer->move_in(std::move(ptr)); }
    void clear() { m_buffer->m_head.store(m_buffer->m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed); }
  };

  struct ConsumerAccess {
   private:
    AIObjectQueue<T>* m_buffer;
   public:
    ConsumerAccess(AIObjectQueue<T>* buffer) : m_buffer(buffer) { buffer->m_consumer_mutex.lock(); }
    ~ConsumerAccess() { m_buffer->m_consumer_mutex.unlock(); }
    int length() const { return m_buffer->consumer_length(); }
    T move_out() { return m_buffer->move_out(); }
    void clear() { m_buffer->m_tail.store(m_buffer->m_head.load(std::memory_order_relaxed), std::memory_order_relaxed); }
  };

  ProducerAccess producer_access() { return ProducerAccess(this); }
  ConsumerAccess consumer_access() { return ConsumerAccess(this); }
};
