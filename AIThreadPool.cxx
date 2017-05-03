#include "sys.h"
#include "debug.h"
#include "AIThreadPool.h"
#include "threadsafe/aithreadsafe.h"
#include "threadsafe/AIReadWriteMutex.h"

//static
std::atomic<AIThreadPool*> AIThreadPool::s_instance;

namespace {

struct Worker;
using worker_function_t = void (*)();
using worker_container_t = std::vector<Worker>;
using workers_t = aithreadsafe::Wrapper<worker_container_t, aithreadsafe::policy::ReadWrite<AIReadWriteMutex>>;

struct Worker {
  // A Worker is only const when we access it from a const worker_container_t.
  // However, the (read) lock on the worker_container_t only protects the internals
  // of the container, not it's elements. So, all elements are considered mutable.
  mutable std::thread m_thread;
  mutable std::atomic_bool m_quit;

  // Construct a new Worker.
  Worker(worker_function_t worker_function) : m_thread(worker_function), m_quit(false) { }

  // The move constructor can only be called as a result of a reallocation, as a result
  // of a size increase of the std::vector<Worker> (because Workers are put into it with
  // emplace_back(), Worker is not copyable, and we never move a Worker out of the vector).
  // That means that at the moment the move constuctor is called we have the exclusive
  // write lock on the vector and therefore no other thread can access this Worker.
  // Therefore it is safe to non-atomically copy m_quit (note that it cannot be moved or
  // copied atomically).
  Worker(Worker&& rvalue) : m_thread(std::move(rvalue.m_thread)), m_quit(rvalue.m_quit.load()) { }

  // Destructor.
  ~Worker()
  {
    // It's ok to use memory_order_relaxed here because this is the
    // same thread that (should have) called quit() in the first place.
    // Call quit() before destructing a Worker.
    ASSERT(m_quit.load(std::memory_order_relaxed));
    // Call join() before destructing a Worker.
    ASSERT(!m_thread.joinable());
  }

 public:
  // Inform the thread that we want it to stop running.
  void quit() const { m_quit.store(true, std::memory_order_relaxed); }

  // Wait for the thread to have exited.
  void join() const
  {
    // It's ok to use memory_order_relaxed here because this is the same thread that (should have) called quit() in the first place.
    // Only call join() on Workers that are quitting.
    ASSERT(m_quit.load(std::memory_order_relaxed));
    // Only call join() once (this should be true for all Worker's that were created and not moved).
    ASSERT(m_thread.joinable());
    m_thread.join();
  }

  // Called from worker thread.
  static int get_handle();
  bool running() const { return !m_quit.load(std::memory_order_acquire); } // We are running as long as m_quit isn't set.
};

// Define a read/write lock protected container with all Workers.
//
// Obtaining and releasing a read lock by constructing and destructing a workers_t::rat object,
// takes 178 ns (without optimization) / 117 ns (with optimization) [measured with microbench
// on a 3.6GHz AMD FX(tm)-8150 core].
//
// [ Note that construcing and destructing a workers_t::wat object, for write access, takes 174 ns
//   respectively 121 ns; although speed is not relevant in that case. ]
//
workers_t workers;

// Mutex to protect critical areas in which conversion from read to write lock is necessary
// (allowing concurrent conversion attempts can cause an exception to be thrown that we
// can't recover from in our case).
std::mutex workers_r_to_w_mutex;

//static
int Worker::get_handle()
{
  workers_t::rat workers_r(workers);
  int const current_number_of_threads = workers_r->size();
  for (int t = 0; t < current_number_of_threads; ++t)
    if (workers_r->at(t).m_thread.get_id() == std::this_thread::get_id())
      return t;
  // We were destructed before we even began; that is, some thread
  // now has the read lock on `workers` and is waiting for us to join().
  return -1;
}

// This is the main function for each of the worker threads.
void worker()
{
  Debug(NAMESPACE_DEBUG::init_thread());
  Dout(dc::threadpool, "Thread started.");

  int const self = Worker::get_handle();
  if (self != -1)
  {
    while(workers_t::rat(workers)->at(self).running())
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  Dout(dc::threadpool, "Thread terminated.");
}

// Add new threads to the already write locked workers container.
void add_threads(workers_t::wat& workers_w, int number)
{
  DoutEntering(dc::threadpool, "add_threads(" << number << ")");
  for (int i = 0; i < number; ++i)
    workers_w->emplace_back(&worker);
}

// Remove threads from the already read locked workers container.
// This function is called inside a criticial area of workers_r_to_w_mutex
// so we may convert the read lock to a write lock.
void remove_threads(workers_t::rat& workers_r, int n)
{
  DoutEntering(dc::threadpool, "remove_threads(" << n << ")");

  // Since we only have a read lock on `workers` we can only
  // get access to const Worker's; in order to allow us to manipulate
  // individual Worker objects anywhere all their members are mutable.
  // This means that we need another mechanism to protect them
  // from concurrent access: all functions calling any method of
  // Worker must be inside the critical area of some mutex.
  // Both, quit() and join() are ONLY called in this function;
  // and this function is only called while in the critical area
  // of workers_r_to_w_mutex; so we're OK.

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
    // Worker threads need to take a read lock on workers, so it
    // would not be smart to have a write lock on that while trying
    // to join them.
    workers_r->back().join();
    // Finally, obtain a write lock and remove/destruct the Worker.
    workers_t::wat(workers_r)->pop_back();
  }
}

} // namespace

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

  workers_t::wat workers_w(workers);
  assert(workers_w->empty());                    // Paranoia; even in the case of constructing a second AIThreadPool
                                                 // after destructing the first, this should be the case here.
  workers_w->reserve(m_max_number_of_threads);   // Attempt to avoid reallocating the vector in the future.
  add_threads(workers_w, number_of_threads);     // Create and run number_of_threads threads.
}

AIThreadPool::~AIThreadPool()
{
  // Construction and destruction is not thread-safe.
  assert(aithreadid::is_single_threaded(m_constructor_id));
  if (m_pillaged) return;                        // This instance was moved. We don't really exist.

  // Kill all threads.
  {
    std::lock_guard<std::mutex> lock(workers_r_to_w_mutex);
    workers_t::rat workers_r(workers);
    remove_threads(workers_r, workers_r->size());
  }
  // Allow construction of another AIThreadPool.
  s_instance = nullptr;
}

void AIThreadPool::change_number_of_threads_to(int number_of_threads)
{
  if (number_of_threads > m_max_number_of_threads)
  {
    Dout(dc::warning, "Increasing number of thread beyond the initially set maximum.");
    workers_t::wat workers_w(workers);
    // This might reallocate the vector and therefore move existing Workers,
    // but that should be ok while holding the write-lock... I think.
    workers_w->reserve(number_of_threads);
    m_max_number_of_threads = number_of_threads;
  }

  // This must be locked before locking workers.
  std::lock_guard<std::mutex> lock(workers_r_to_w_mutex);
  // Kill or add threads.
  workers_t::rat workers_r(workers);
  int current_number_of_threads = workers_r->size();
  if (number_of_threads < current_number_of_threads)
    remove_threads(workers_r, current_number_of_threads - number_of_threads);
  else if (number_of_threads > current_number_of_threads)
  {
    workers_t::wat workers_w(workers_r);
    add_threads(workers_w, number_of_threads - current_number_of_threads);
  }
}

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
channel_ct threadpool("THREADPOOL");
NAMESPACE_DEBUG_CHANNELS_END
#endif
