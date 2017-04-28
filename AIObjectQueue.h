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

#include <mutex>
#include <atomic>
#include <functional>
#include <cstdlib>
#include <cstdint>

// A ring buffer for moveable objects.
//
// Usage (using for example std::function<void()>):
//
// AIObjectQueue<std::function<void()>> queue(40);          // 40 std::function<void()> objects.
//
// // Producer threads:
// if (!queue.producer_access().move_in([](){return 42;}))  // The temporary returned by producer_access() locks producer access (consumers can still continue reading).
// { /* Buffer full */
//
// // Consumer threads:
// auto ca = queue.consumer_access();                       // Lock consumer access (producers can still continue writing).
// std::function<void()> f(ca.move_out());
// if (!f) { /* buffer empty */ }
//
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

 private:
  void allocate_(int objects)
  {
    // Allocate storage aligned to at least 32 bytes.
    void* storage;
    int ret = posix_memalign(&storage, alignment, objects * sizeof(T));
    Dout(dc::malloc, "storage = " << storage);
    if (ret != 0)
    {
      Dout(dc::warning, "posix_memalign(" << &storage << ", " << alignment << ", " << objects * sizeof(T) << ") returned " << ret);
      throw std::bad_alloc();
    }
    m_start = static_cast<T*>(storage);
    Dout(dc::malloc, "m_start = " << (void*)m_start);
    m_capacity = objects;
    for (size_t i = 0; i < m_capacity; ++i)
      new (&m_start[i]) T;
    // Clean
    m_tail = 0;
    m_head = 0;
  }

  void deallocate_()
  {
    T* object = m_start;
    for (size_t i = 0; i < m_capacity; ++i)
      object[i].~T();
    free(m_start);
  }

  // Return index advanced one chunk.
  size_t increment(size_type index) const
  {
    // This is branchless and much faster than using modulo.
    return index + 1 == m_capacity ? 0 : index + 1;
  }

  //-------------------------------------------------------------------------
  // Producer thread.

  // Accessed by ProducerAccess.
  bool move_in(T&& object)
  {
    auto const current_head = m_head.load(std::memory_order_relaxed);
    auto const next_head    = increment(current_head);

    if (next_head != m_tail.load(std::memory_order_acquire))  // Otherwise the buffer would appear empty after the m_head.store below,
                                                              // and we'd be writing over data that possibly still needs to be read
                                                              // by the consumer (that was returned by move_out()).
    {
      m_start[current_head] = std::move(object);
      m_head.store(next_head, std::memory_order_release);
      return true; // object was moved to the queue.
    }

    return false;  // Full queue.
  }

  //-------------------------------------------------------------------------
  // Consumer thread.

  // Accessed by ConsumerAccess.
  T move_out()
  {
    auto const current_tail = m_tail.load(std::memory_order_relaxed);
    if (current_tail != m_head.load(std::memory_order_acquire))         // Queue not empty?
    {
      auto const next_tail = increment(current_tail);
      m_tail.store(next_tail, std::memory_order_release);
      return std::move(m_start[current_tail]);
    }
    return T();  // Empty queue.
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
    bool move_in(T&& ptr) { return m_buffer->move_in(std::move(ptr)); }
    void clear() { m_buffer->m_head.store(m_buffer->m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed); }
  };

  struct ConsumerAccess {
   private:
    AIObjectQueue<T>* m_buffer;
   public:
    ConsumerAccess(AIObjectQueue<T>* buffer) : m_buffer(buffer) { buffer->m_consumer_mutex.lock(); }
    ~ConsumerAccess() { m_buffer->m_consumer_mutex.unlock(); }
    T move_out() { return m_buffer->move_out(); }
    void clear() { m_buffer->m_tail.store(m_buffer->m_head.load(std::memory_order_relaxed), std::memory_order_relaxed); }
  };

  ProducerAccess producer_access() { return ProducerAccess(this); }
  ConsumerAccess consumer_access() { return ConsumerAccess(this); }
};
