#include "sys.h"
#include "debug.h"
#include "AIThreadPool.h"

//static
std::atomic<AIThreadPool*> AIThreadPool::s_instance;

//static
void AIThreadPool::Worker::main(int const self)
{
  Debug(NAMESPACE_DEBUG::init_thread());
  Dout(dc::threadpool, "Thread started.");

  // TODO: for now assume there is only one queue. Wait until it appears.
  while (AIThreadPool::instance().queues_read_access()->size() == 0)
  {
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    continue;
  }

  while(workers_t::rat(AIThreadPool::instance().m_workers)->at(self).running())
  {
    std::function<void()> f;
    int length;
    { // Lock the queue for other consumer threads.
      auto queues_r = AIThreadPool::instance().queues_read_access();
      queues_container_t::value_type& queue = queues_r->at(0);
      auto access = queue.consumer_access();
      length = access.length();
      if (length > 0)
        f = access.move_out();
    } // Unlock the queue.
    if (length > 0)
      f(); // Invoke the functor.
    else
      std::this_thread::sleep_for(std::chrono::microseconds(10));       // FIXME
  }

  Dout(dc::threadpool, "Thread terminated.");
}

//static
void AIThreadPool::add_threads(workers_t::wat& workers_w, int current_number_of_threads, int requested_number_of_threads)
{
  DoutEntering(dc::threadpool, "add_threads(" << current_number_of_threads << ", " << requested_number_of_threads << ")");
  for (int i = current_number_of_threads; i < requested_number_of_threads; ++i)
    workers_w->emplace_back(&Worker::main, i);
}

// This function is called inside a criticial area of m_workers_r_to_w_mutex
// so we may convert the read lock to a write lock.
//static
void AIThreadPool::remove_threads(workers_t::rat& workers_r, int n)
{
  DoutEntering(dc::threadpool, "remove_threads(" << n << ")");

  // Since we only have a read lock on `m_workers` we can only
  // get access to const Worker's; in order to allow us to manipulate
  // individual Worker objects anywhere all their members are mutable.
  // This means that we need another mechanism to protect them
  // from concurrent access: all functions calling any method of
  // Worker must be inside the critical area of some mutex.
  // Both, quit() and join() are ONLY called in this function;
  // and this function is only called while in the critical area
  // of m_workers_r_to_w_mutex; so we're OK.

  // Call quit() on the n last threads in the container.
  int t = workers_r->size();
  for (int i = 0; i < n; ++i)
    workers_r->at(--t).quit();
  // If the relaxed stores to the m_quit's is very slow then we might
  // be calling join() on threads before they can see their m_quit
  // flag being set. This is not a problem. However, theoretically
  // the store could be delayed forever, so to be formerly correct,
  // lets flush all stores here before calling join().
  std::atomic_thread_fence(std::memory_order_release);
  // Join the n last threads in the container.
  for (int i = 0; i < n; ++i)
  {
    // Worker threads need to take a read lock on m_workers, so it
    // would not be smart to have a write lock on that while trying
    // to join them.
    workers_r->back().join();
    // Finally, obtain a write lock and remove/destruct the Worker.
    workers_t::wat(workers_r)->pop_back();
  }
}

AIThreadPool::AIThreadPool(int number_of_threads, int max_number_of_threads) :
    m_constructor_id(aithreadid::none), m_max_number_of_threads(std::max(number_of_threads, max_number_of_threads)), m_pillaged(false)
{
  // Only here to record the id of the thread who constructed us.
  // Do not create a second AIThreadPool from another thread;
  // use AIThreadPool::instance() to access the thread pool created from main.
  assert(aithreadid::is_single_threaded(m_constructor_id));

  // Only construct ONE AIThreadPool, preferably somewhere at the beginning of main().
  // If you want more than one thread pool instance, don't. One is enough and much
  // better than having two or more.
  assert(s_instance == nullptr);

  // Allow access to the thread pool from everywhere without having to pass it around.
  s_instance = this;

  workers_t::wat workers_w(m_workers);
  assert(workers_w->empty());                    // Paranoia; even in the case of constructing a second AIThreadPool
                                                 // after destructing the first, this should be the case here.
  workers_w->reserve(m_max_number_of_threads);   // Attempt to avoid reallocating the vector in the future.
  add_threads(workers_w, 0, number_of_threads);  // Create and run number_of_threads threads.
}

AIThreadPool::~AIThreadPool()
{
  // Construction and destruction is not thread-safe.
  assert(aithreadid::is_single_threaded(m_constructor_id));
  if (m_pillaged) return;                        // This instance was moved. We don't really exist.

  // Kill all threads.
  {
    std::lock_guard<std::mutex> lock(m_workers_r_to_w_mutex);
    workers_t::rat workers_r(m_workers);
    remove_threads(workers_r, workers_r->size());
  }
  // Allow construction of another AIThreadPool.
  s_instance = nullptr;
}

void AIThreadPool::change_number_of_threads_to(int requested_number_of_threads)
{
  if (requested_number_of_threads > m_max_number_of_threads)
  {
    Dout(dc::warning, "Increasing number of thread beyond the initially set maximum.");
    workers_t::wat workers_w(m_workers);
    // This might reallocate the vector and therefore move existing Workers,
    // but that should be ok while holding the write-lock... I think.
    workers_w->reserve(requested_number_of_threads);
    m_max_number_of_threads = requested_number_of_threads;
  }

  // This must be locked before locking m_workers.
  std::lock_guard<std::mutex> lock(m_workers_r_to_w_mutex);
  // Kill or add threads.
  workers_t::rat workers_r(m_workers);
  int current_number_of_threads = workers_r->size();
  if (requested_number_of_threads < current_number_of_threads)
    remove_threads(workers_r, current_number_of_threads - requested_number_of_threads);
  else if (requested_number_of_threads > current_number_of_threads)
  {
    workers_t::wat workers_w(workers_r);
    add_threads(workers_w, current_number_of_threads, requested_number_of_threads);
  }
}

int AIThreadPool::new_queue(int capacity, int priority)
{
  DoutEntering(dc::threadpool, "AIThreadPool::new_queue(" << capacity << ", " << priority << ")");
  queues_t::wat queues_w(m_queues);
  size_t index = queues_w->size();
  queues_w->emplace_back(std::move(queues_container_t::value_type(capacity)));
  Dout(dc::threadpool, "Returning index " << index << "; size is now " << queues_w->size() << " for std::vector<> at " << (void*)&*queues_w);
  return index;
}

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
channel_ct threadpool("THREADPOOL");
NAMESPACE_DEBUG_CHANNELS_END
#endif
