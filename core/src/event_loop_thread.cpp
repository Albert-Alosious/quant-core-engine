#include "quant/concurrent/event_loop_thread.hpp"
#include <chrono>

namespace quant {

namespace {

// How long the worker thread waits when the queue is empty before re-checking
// running_. Short enough that stop() is responsive; long enough to avoid
// busy-wait. 10 ms is a reasonable default for a trading engine event loop.
constexpr auto kIdleWaitTimeout = std::chrono::milliseconds(10);

}  // namespace

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
// Ensures the worker thread is stopped before we destroy the queue, bus, and
// sync primitives. Otherwise the thread might still be in run() and access
// destroyed members. Call stop() which sets running_ = false, notifies, and
// joins. If the user already called stop(), thread_ is not joinable and we
// do nothing.
// -----------------------------------------------------------------------------
EventLoopThread::~EventLoopThread() { stop(); }

// -----------------------------------------------------------------------------
// start()
// -----------------------------------------------------------------------------
void EventLoopThread::start() {
  // If the thread is already running (or was started and not yet joined),
  // do nothing. This makes start() idempotent and avoids starting two
  // worker threads for the same object.
  if (thread_.joinable()) {
    return;
  }

  // running_ must be true before we start the thread so the worker sees
  // running_ == true on its first check. std::memory_order_seq_cst is the
  // default; ensures visibility across threads.
  running_.store(true);

  // std::thread constructor takes a callable and arguments. Here we pass a
  // lambda that captures [this] and calls run(). The thread starts
  // immediately; run() is the loop that pops and publishes until running_
  // becomes false.
  thread_ = std::thread([this] { run(); });
}

// -----------------------------------------------------------------------------
// stop()
// -----------------------------------------------------------------------------
void EventLoopThread::stop() {
  // If the thread was never started or was already stopped and joined,
  // there is nothing to do. joinable() is false in both cases.
  if (!thread_.joinable()) {
    return;
  }

  // Signal the worker to exit. The worker checks running_ in its loop
  // condition and in the wait_for predicate. After this store, the worker
  // will exit the loop the next time it checks.
  running_.store(false);

  // Wake the worker if it is blocked in wait_for(). Without this, the worker
  // might wait up to kIdleWaitTimeout before re-checking running_.
  // notify_all() is used even though there is one waiter; it is correct and
  // clear. We do not need to hold stop_mutex_ when notifying (some
  // implementations may be slightly more efficient when not holding the lock).
  stop_cv_.notify_all();

  // Wait for the worker thread to finish. The worker exits run() and the
  // thread completes. join() blocks until the thread has finished. We must
  // not hold stop_mutex_ (or any lock that the worker might need) across
  // join(), otherwise we could deadlock. Here we hold no lock.
  thread_.join();
}

// -----------------------------------------------------------------------------
// run() — worker loop
// -----------------------------------------------------------------------------
void EventLoopThread::run() {
  while (running_.load()) {
    // Non-blocking pop. If the queue has an event, we get it and publish.
    // If the queue is empty, we fall through to the wait below. Using
    // try_pop() (instead of queue_.pop()) ensures we can wake on stop_cv_
    // and exit; blocking pop() would not wake on our condition variable.
    std::optional<Event> event = queue_.try_pop();

    if (event) {
      // Publish on this thread. All subscribers run here, so event handling
      // is serialized on the loop thread. No locks held during publish()
      // (EventBus copies subscriber list before invoking callbacks).
      bus_.publish(*event);
      continue;
    }

    // Queue was empty. Wait for either a short timeout or until stop() notifies.
    // unique_lock is required because condition_variable::wait_for() must
    // unlock the mutex while waiting and re-lock when it returns. The
    // predicate [this] { return !running_.load(); } is checked before waiting
    // and after any wakeup; if running_ is false we exit wait_for and then
    // exit the loop. This handles both timeout and explicit notify in stop().
    std::unique_lock lock(stop_mutex_);
    stop_cv_.wait_for(lock, kIdleWaitTimeout,
                     [this] { return !running_.load(); });
  }
}

}  // namespace quant
