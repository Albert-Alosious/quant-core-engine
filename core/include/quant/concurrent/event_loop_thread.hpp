#pragma once

#include "quant/concurrent/thread_safe_queue.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace quant {

// -----------------------------------------------------------------------------
// EventLoopThread
// -----------------------------------------------------------------------------
// Responsibility: Owns a single worker thread that continuously drains a
// ThreadSafeQueue<Event> and publishes each event to an EventBus on that
// thread. Other threads push events via push(); subscribers to the bus run
// only on this thread, so event handling is serialized on the loop thread.
//
// Why in architecture: Implements the "event loop per thread" pattern from
// the threading model (architecture.md): e.g. Strategy Thread or Risk +
// Execution Thread. Other components push events into the queue; this thread
// dispatches them via the bus. No global state—each loop owns its queue and
// bus and is passed by reference to components that need it.
//
// Thread model: The worker runs in the owned std::thread. start() and stop()
// may be called from any thread. push() is thread-safe and may be called
// from any thread. All EventBus subscriber callbacks run on the loop thread.
// -----------------------------------------------------------------------------
class EventLoopThread {
 public:
  EventLoopThread() = default;

  // Destructor must stop the thread so we do not destroy the object while
  // the worker is still running. Joining in the destructor is the standard
  // RAII approach for thread ownership.
  ~EventLoopThread();

  // Non-copyable and non-movable: owns thread, mutex, condition_variable,
  // queue, and bus. Copying or moving would duplicate or invalidate these.
  EventLoopThread(const EventLoopThread&) = delete;
  EventLoopThread& operator=(const EventLoopThread&) = delete;
  EventLoopThread(EventLoopThread&&) = delete;
  EventLoopThread& operator=(EventLoopThread&&) = delete;

  // -------------------------------------------------------------------------
  // start()
  // -------------------------------------------------------------------------
  // What: Starts the worker thread. The thread runs until stop() is called.
  // If the thread is already running, does nothing (idempotent).
  // Why: Call once after construction to begin processing events. Clean
  // lifecycle: construct, start(), use push() and eventBus(), then stop().
  // Thread-safety: Safe to call from any thread. Starting twice is a no-op.
  // Input: None.
  // Output: None.
  // -------------------------------------------------------------------------
  void start();

  // -------------------------------------------------------------------------
  // stop()
  // -------------------------------------------------------------------------
  // What: Signals the worker to exit, waits for it to finish (join), then
  // returns. After stop(), the thread is no longer running; start() may be
  // called again to restart.
  // Why: Clean shutdown. We set running_ = false and notify the condition
  // variable so the worker wakes from its wait (if it was idle). The worker
  // exits its loop and join() returns. We do not hold any lock across
  // join(), so no deadlock.
  // Thread-safety: Safe to call from any thread. Idempotent: if already
  // stopped, join() is skipped (thread_ is not joinable).
  // Input: None.
  // Output: None.
  // -------------------------------------------------------------------------
  void stop();

  // -------------------------------------------------------------------------
  // push(event)
  // -------------------------------------------------------------------------
  // What: Enqueues one event. The worker thread will eventually pop it and
  // publish it on the EventBus (on the loop thread).
  // Why: This is the only way for other threads to submit work to this loop.
  // Call from market data thread, strategy thread, etc. Thread-safe.
  // Thread-safety: Safe to call from any thread. Forwards to queue_.push().
  // Input: event — copied or moved into the queue.
  // Output: None.
  // -------------------------------------------------------------------------
  void push(Event event) { queue_.push(std::move(event)); }

  // -------------------------------------------------------------------------
  // eventBus()
  // -------------------------------------------------------------------------
  // What: Returns a reference to the owned EventBus. Callers use it to
  // subscribe() so their callbacks run when events are published on this
  // loop thread.
  // Why: Subscribers must register with the bus that this loop publishes to.
  // Returning a reference keeps ownership inside EventLoopThread; no shared
  // ownership or raw new/delete.
  // Thread-safety: Safe to call from any thread. The bus is thread-safe for
  // subscribe/unsubscribe/publish; publish is only called from the loop
  // thread, but subscribe/unsubscribe may be called from others.
  // Input: None.
  // Output: EventBus& — valid for the lifetime of this EventLoopThread.
  // -------------------------------------------------------------------------
  EventBus& eventBus() { return bus_; }

  // Const overload for callers that only need to pass the bus elsewhere
  // without subscribing (e.g. for logging or inspection).
  const EventBus& eventBus() const { return bus_; }

 private:
  // -------------------------------------------------------------------------
  // run() — worker thread entry point
  // -------------------------------------------------------------------------
  // What: Loop: try_pop() from queue; if event, publish to bus; else wait
  // on condition variable with short timeout, then re-check running_.
  // Why: try_pop() instead of blocking pop() so we can wake on stop_requested
  // without adding a "close" to ThreadSafeQueue. wait_for() with timeout
  // keeps the loop responsive to stop() while avoiding busy-wait.
  // Runs only on the worker thread. Called by start() via std::thread.
  // -------------------------------------------------------------------------
  void run();

  // Queue of events. Other threads push; the worker thread pops and publishes.
  ThreadSafeQueue<Event> queue_;

  // Bus owned by this loop. Worker calls bus_.publish() on the loop thread.
  EventBus bus_;

  // When true, the worker keeps running. Set false in stop() to exit the loop.
  // std::atomic so the worker can read it without locking; stop() writes once.
  std::atomic<bool> running_{false};

  // Protects the condition variable. The worker waits on stop_cv_ with this
  // mutex so that stop() can notify and the worker wakes.
  std::mutex stop_mutex_;

  // Signalled in stop() so the worker wakes from wait_for() and re-checks
  // running_. Without this, the worker might be blocked in wait_for() for up
  // to the timeout even after stop() is called.
  std::condition_variable stop_cv_;

  // The worker thread. Started in start(), joined in stop() or destructor.
  // joinable() means the thread is running or has finished but not yet joined.
  std::thread thread_;
};

}  // namespace quant
