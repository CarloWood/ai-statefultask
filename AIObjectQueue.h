/**
 * @file
 * @brief A FIFO buffer for moveable objects.
 *
 * @Copyright (C) 2017  Carlo Wood.
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
#include "debug.h"
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdlib>
#include <cstdint>


/*!
 * @brief A ring buffer for moveable objects.
 *
 * Usage (using for example <code>std::function&lt;void()&gt;</code>):
 *
 * @code
 * constexpr int capacity = 8;
 * AIObjectQueue<std::function<void()>> queue(capacity);    // Allocates 9 std::function<void()> objects.
 * @endcode
 *
 * The actual size of the allocated memory for this
 * queue will be nine: eight objects can be written
 * to the queue without reading anything from it.
 * The space for the nineth object is reserved to
 * store the last object that was already read from
 * the queue but wasn't popped yet.
 */
//! @code
//! // Producer threads:
//! { // Lock the queue for other producer threads.
//!   auto access = queue.producer_access();
//!   int length = access.length();
//!   if (length == capacity) { /* Buffer full */ return; }
//!   access.move_in([](){ std::cout << "Hello\n"; });
//! } // Unlock the queue.
//! @endcode
/*!
 * The value of \c length allows one to do client-side
 * bandwidth control; by reducing the throughput till
 * the returned length is as small as possible one can
 * reduce the latency.
 *
 * Note that due to race conditions, the actual length
 * might be less, as consumer threads can read from the
 * queue while we hold the producer lock.
 */
//! @code
//! // Consumer threads:
//! std::function<void()> f;
//! { // Lock the queue for other consumer threads.
//!   auto access = queue.consumer_access();
//!   int length = access.length();
//!   if (length == 0) { /* Buffer empty */ return; }
//!   f = access.move_out();
//! } // Unlock the queue.
//! f(); // Invoke the functor.
//! @endcode
/*!
 * Here the actual length might be greater than the
 * returned value because producer threads can still
 * write to the queue while we hold the consumer lock.
 * In fact, both calls to \c length() do essentially the same
 * calculation (which doesn't require locking at all to be
 * thread-safe); the only difference is the memory model
 * used for access, but the locking by accessing \c length()
 * through the respective access objects is necessary to
 * prevent a producer thread having a race condition with
 * another producer thread, or a consumer thread having a
 * race condition with another consumer thread.
 *
 * <h3>const qualifier and %AIObjectQueue&lt;T&gt;</h3>
 *
 * Essentially, an %AIObjectQueue only allows one to move
 * objects of type \c T into and out of the queue;
 * it doesn't have accessors.
 * Consequently none of its member functions is const.
 *
 * Therefore, it is possible to overload the use of the const
 * qualifier for the public member functions to mean: these
 * member functions are threadsafe; they may be accessed
 * concurrently by multiple threads.
 *
 * Specifically, producer_access() and consumer_access(),
 * although \c const, <em>do</em> allow respectively writing data into \htmlonly&dash;\endhtmlonly
 * and extracting data from the queue. Their const-ness merely means that concurrent
 * access is thread safe.
 */
template<typename T>
class AIObjectQueue
{
  using size_type = std::uint_fast32_t;   // "4294967295 objects ought to be enough for anybody." --Bill Gates.

 private:
  T* m_start;                       // Start of buffer.
  size_type m_capacity;             // Number of objects of type T that can be written to the buffer without reading.
  std::atomic<size_type> m_head;    // Next write position.
  std::atomic<size_type> m_tail;    // Next read position.
  std::mutex m_producer_mutex;
  std::mutex m_consumer_mutex;

 public:
  //! Buffer storage alignment. Align the buffer at a multiple of the L1 cache line size.
  static constexpr size_t alignment = (alignof(T) < 64) ? (size_t)64 : alignof(T);

 public:
  //! Construct a buffer with size zero.
  AIObjectQueue() : m_start(nullptr), m_capacity(0), m_head(0), m_tail(0) { }

  //! Construct a buffer with a capacity of \a capacity objects of type \c T.
  AIObjectQueue(int capacity) : m_start(nullptr), m_capacity(0), m_head(0), m_tail(0) { allocate_(capacity); }

  //! Move constructor. Only use this directly after constructing (if at all).
  AIObjectQueue(AIObjectQueue&& rvalue) : m_start(rvalue.m_start), m_capacity(rvalue.m_capacity), m_head(0), m_tail(0)
  {
    // Should only ever move an AIObjectQueue directly after constructing it.
    ASSERT(rvalue.m_head == 0 && rvalue.m_tail == 0);
    // Make sure the rvalue's destructor won't deallocate.
    rvalue.m_capacity = 0;
  }

  //! Destructor -- frees allocated memory.
  ~AIObjectQueue() { if (m_capacity) deallocate_(); }

  //! Returns the current capacity of the buffer.
  int capacity() const { return m_capacity; }

 private:
  void allocate_(int const capacity)
  {
    // Whatever...
    if (AI_UNLIKELY(capacity <= 0))
    {
      Dout(dc::warning, "Calling AIObjectQueue::allocate_(" << capacity << ")");
      // Bring object to state of default constructed.
      ASSERT(m_start == nullptr && m_capacity == 0);
      m_head = m_tail = 0;
      return;
    }

    // Allocate storage aligned to at least 64 bytes.
    void* storage;
    // Allocate one object more than requested.
    int ret = posix_memalign(&storage, alignment, (capacity + 1) * sizeof(T));
    Dout(dc::malloc, "storage = " << storage);
    if (ret != 0)
    {
      Dout(dc::warning, "posix_memalign(" << &storage << ", " << alignment << ", " << (capacity + 1) * sizeof(T) << ") returned " << ret);
      throw std::bad_alloc();
    }
    m_start = static_cast<T*>(storage);
    Dout(dc::malloc, "m_start = " << (void*)m_start);
    m_capacity = capacity;
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
    // Call reallocate() after a default construction, or pass the capacity of the queue (in objects) when constructing it.
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
  /*!
   * @brief Change the capacity of the buffer.
   *
   * This function will deadlock when used by a thread that still has
   * the return type of a call to \c producer_access() or \c consumer_access()
   * around.
   *
   * This blocks all producers and consumers from using the buffer while
   * (re)allocating memory and moving memory content. It is currently also
   * destructive to all objects still in the buffer. Hence this function really
   * only should be used while it is empty and not in use at all.
   *
   * @param capacity The new queue capacity in number of objects of type \c T.
   */
  void reallocate(int capacity)
  {
    // Buffer may not be in use (these locks also protect access to the buffer itself).
    std::lock_guard<std::mutex> lock1(m_producer_mutex);
    std::lock_guard<std::mutex> lock2(m_consumer_mutex);
    // Allow reallocation.
    if (m_capacity) deallocate_();
    allocate_(capacity);
  }

  //! The return type of producer_access().
  struct ProducerAccess
  {
   private:
    AIObjectQueue<T>* m_buffer;

   public:
    //! Constructor.
    ProducerAccess(AIObjectQueue<T>* buffer) : m_buffer(buffer) { buffer->m_producer_mutex.lock(); }

    //! Destructor.
    ~ProducerAccess() { m_buffer->m_producer_mutex.unlock(); }

    /*!
     * @brief Return the current length of the buffer.
     *
     * This is the number of objects stored in the buffer.
     * If the returned value equals the capacity of the buffer
     * than the buffer is full and one should not call move_in.
     */
    int length() const { return m_buffer->producer_length(); }

    /*!
     * @brief Write an object into the buffer by means of \c std::move.
     *
     * @param object The object to move into the buffer.
     */
    void move_in(T&& object) { m_buffer->move_in(std::move(object)); }

    /*!
     * @brief Clear the buffer by putting the head where the tail is.
     *
     * Artificially stop all consumers from reading this buffer
     * by faking that it is now empty.
     * This should probably only be used as part of a clean up.
     * The objects that weren't read are not destroyed;
     * they will be destroyed when the buffer is destroyed, or,
     * if one continues to use the buffer, they would be partially
     * destroyed when another object is moved over them (the
     * destructor is always only called once the buffer is
     * destructed or reallocate() is called).
     */
    void clear() { m_buffer->m_head.store(m_buffer->m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed); }
  };

  //! The return type of consumer_access().
  struct ConsumerAccess
  {
   private:
    AIObjectQueue<T>* m_buffer;

   public:
    //! Constructor.
    ConsumerAccess(AIObjectQueue<T>* buffer) : m_buffer(buffer) { buffer->m_consumer_mutex.lock(); }

    //! Destructor.
    ~ConsumerAccess() { m_buffer->m_consumer_mutex.unlock(); }
    /*!
     * @brief Return the current length of the buffer.
     *
     * This is the number of objects available for reading.
     * If the returned value equals zero than the buffer is empty and one should not call move_out.
     */

    int length() const { return m_buffer->consumer_length(); }
    /*!
     * @brief Read an object from the buffer by means of \c std::move.
     *
     * @returns The read object.
     */

    T move_out() { return m_buffer->move_out(); }
    /*!
     * @brief Clear the buffer by putting the tail where the head is.
     *
     * Artificially stop all consumers from reading this buffer
     * by faking that it is now empty.
     * This should probably only be used as part of a clean up.
     * The objects that weren't read are not destroyed;
     * they will be destroyed when the buffer is destroyed, or,
     * if one continues to use the buffer, they would be partially
     * destroyed when another object is moved over them (the
     * destructor is always only called once the buffer is
     * destructed or reallocate() is called).
     */

    void clear() { m_buffer->m_tail.store(m_buffer->m_head.load(std::memory_order_relaxed), std::memory_order_relaxed); }
  };

  /*!
   * @brief Obtain exclusive producer access to the buffer.
   *
   * @returns A ProducerAccess object.
   */
  ProducerAccess producer_access() const { return ProducerAccess(const_cast<AIObjectQueue<T>*>(this)); }

  /*!
   * @brief Obtain exclusive consumer access to the buffer.
   *
   * @returns A ConsumerAccess object.
   */
  ConsumerAccess consumer_access() const { return ConsumerAccess(const_cast<AIObjectQueue<T>*>(this)); }
};
