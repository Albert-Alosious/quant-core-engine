#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace quant {

// -----------------------------------------------------------------------------
// ThreadSafeQueue<T>
// -----------------------------------------------------------------------------
// Responsibility: A FIFO queue that multiple threads can push to and pop from
// without data races. Provides blocking pop() (wait until an item is available)
// and non-blocking try_pop() (return immediately with item or empty).
//
// Why in architecture: Used at thread boundaries (architecture.md threading
// model: market data thread, strategy thread, risk+execution thread). One thread
// pushes events or work items; another thread pops them. No global mutable
// state—each queue is an object passed into the components that need it.
//
// Thread model: Not tied to any specific thread. Safe for multiple producers
// and multiple consumers. Blocking pop() may block the calling thread until
// another thread pushes. All methods are thread-safe.
// -----------------------------------------------------------------------------
template <typename T>
class ThreadSafeQueue {
 public:
  ThreadSafeQueue() = default;

  // Non-copyable: mutex and condition_variable cannot be copied. Copying
  // would require duplicating the queue contents and sync primitives, which
  // is error-prone and rarely needed.
  ThreadSafeQueue(const ThreadSafeQueue&) = delete;
  ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

  // Non-movable: std::mutex and std::condition_variable are not movable in
  // standard C++. Moving the queue would leave sync primitives in an
  // unspecified state. Pass the queue by reference or pointer to share between
  // threads.
  ThreadSafeQueue(ThreadSafeQueue&&) = delete;
  ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

  // -------------------------------------------------------------------------
  // push(value)
  // -------------------------------------------------------------------------
  // What: Appends one item to the back of the queue. If a thread is blocked
  // in pop(), it will be woken so it can consume this item.
  // Why: Producers (e.g. market data thread) call push() to hand work to
  // another thread without blocking indefinitely.
  // Thread-safety: Safe to call from any thread. Mutex protects the deque;
  // after adding, we notify one waiting consumer (if any).
  // Input: value — copied or moved into the queue (T is taken by value so
  // callers can std::move to avoid copies).
  // Output: None.
  // -------------------------------------------------------------------------
  void push(T value) {
    {
      // Scoped lock: we hold the mutex only while modifying the queue.
      // std::lock_guard is RAII; no manual unlock. Prevents forgetting to
      // unlock on exception or early return.
      std::lock_guard lock(mutex_);
      queue_.push_back(std::move(value));
    }
    // Notify outside the lock so the woken thread does not immediately
    // block trying to acquire the same mutex (minor optimization and
    // avoids thundering herd in some implementations).
    condition_.notify_one();
  }

  // -------------------------------------------------------------------------
  // pop() — blocking
  // -------------------------------------------------------------------------
  // What: Removes and returns the front item. If the queue is empty, blocks
  // the calling thread until another thread pushes an item (or until
  // spurious wakeup; we re-check the condition in a loop).
  // Why: Consumer threads (e.g. strategy thread) call pop() to get work;
  // blocking avoids busy-wait and simplifies the consumer loop.
  // Thread-safety: Safe to call from any thread. Waits on condition_variable
  // so only one waiter is woken per push (notify_one). The wait is in a loop
  // because condition_variable can have spurious wakeups—we must re-check
  // !queue_.empty() after waking.
  // Input: None.
  // Output: T — the front item (moved out of the queue). Caller must ensure
  // T is movable or copyable.
  // -------------------------------------------------------------------------
  T pop() {
    std::unique_lock lock(mutex_);
    // Wait until queue is non-empty. unique_lock is required (not lock_guard)
    // because condition_variable::wait() must unlock the mutex while waiting
    // and re-lock when woken. The lambda is the predicate: we only leave wait
    // when queue_ is not empty. This handles spurious wakeups.
    condition_.wait(lock, [this] { return !queue_.empty(); });

    // We hold the lock and queue_ is non-empty. Take front and pop.
    T value = std::move(queue_.front());
    queue_.pop_front();
    return value;
  }

  // -------------------------------------------------------------------------
  // try_pop() — non-blocking
  // -------------------------------------------------------------------------
  // What: If the queue has at least one item, removes the front item and
  // returns it wrapped in std::optional. If the queue is empty, returns
  // std::nullopt immediately without blocking.
  // Why: Allows a consumer to poll when it has other work (e.g. check queue
  // then do something else) or to drain the queue without blocking.
  // Thread-safety: Safe to call from any thread. Short critical section:
  // lock, check empty, optionally take front and pop, unlock.
  // Input: None.
  // Output: std::optional<T> — contains the item if one was available,
  // otherwise std::nullopt.
  // -------------------------------------------------------------------------
  std::optional<T> try_pop() {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    // Move the front out so we do not copy T unnecessarily. optional can hold
    // T by move. After this, queue_.front() is undefined until we pop.
    T value = std::move(queue_.front());
    queue_.pop_front();
    return value;
  }

  // -------------------------------------------------------------------------
  // empty()
  // -------------------------------------------------------------------------
  // What: Returns true if the queue currently has no elements. Snapshot only—
  // another thread may push or pop immediately after.
  // Why: Sometimes useful for logging or conditional logic. Prefer try_pop()
  // for consumer loops when you need to know if an item was present.
  // Thread-safety: Safe to call from any thread. Must hold mutex while
  // reading queue_.empty().
  // Input: None.
  // Output: bool — true if queue was empty at the time of the check.
  // -------------------------------------------------------------------------
  bool empty() const {
    std::lock_guard lock(mutex_);
    return queue_.empty();
  }

 private:
  // Mutex protects queue_ and is used with condition_. All operations that
  // read or modify queue_ must hold mutex_. mutable so we can lock in empty()
  // const; the logical state "queue contents" is still const in empty().
  mutable std::mutex mutex_;

  // Signalled when an item is added. Blocking pop() waits on this until
  // queue_ is non-empty. We use one condition for "not empty" only; no
  // "not full" because the queue is unbounded.
  std::condition_variable condition_;

  // Underlying FIFO storage. std::deque supports O(1) front, pop_front,
  // and push_back. We could use std::queue<T> (default container is deque)
  // but then we would use front() and pop() which returns void—we would
  // need to read front() before pop() anyway. Using deque directly is
  // explicit and matches our usage.
  std::deque<T> queue_;
};

}  // namespace quant
