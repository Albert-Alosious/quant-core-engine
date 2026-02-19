// =============================================================================
// thread_safe_queue_test.cpp
// =============================================================================
// Unit tests for quant::ThreadSafeQueue<T>.
//
// Validates:
//   - Basic FIFO semantics (push/pop ordering)
//   - Non-blocking try_pop() behaviour on empty and non-empty queues
//   - Blocking pop() correctly waits for a producer to push
//   - Thread-safety under concurrent multi-producer / multi-consumer load
//
// Threading model:
//   Several tests spawn threads to exercise the queue's synchronisation.
//   All threads are joined before assertions, so there are no dangling
//   threads if a test fails.
// =============================================================================

#include "quant/concurrent/thread_safe_queue.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <numeric>
#include <optional>
#include <set>
#include <thread>
#include <vector>

// =============================================================================
// Test fixture: provides a fresh int queue for each test.
// =============================================================================
class ThreadSafeQueueTest : public ::testing::Test {
 protected:
  quant::ThreadSafeQueue<int> queue;
};

// -----------------------------------------------------------------------------
// 1. A newly constructed queue must report itself as empty.
// Why: Guarantees zero-initialisation — no leftover memory artefacts.
// -----------------------------------------------------------------------------
TEST_F(ThreadSafeQueueTest, EmptyOnConstruction) {
  EXPECT_TRUE(queue.empty());
}

// -----------------------------------------------------------------------------
// 2. Push one item, pop it, and verify the value survives the round trip.
// Why: The most basic correctness contract — data in == data out.
// -----------------------------------------------------------------------------
TEST_F(ThreadSafeQueueTest, PushAndPopSingle) {
  queue.push(42);

  EXPECT_FALSE(queue.empty());
  int value = queue.pop();
  EXPECT_EQ(value, 42);
  EXPECT_TRUE(queue.empty());
}

// -----------------------------------------------------------------------------
// 3. Push multiple items and verify they come back in FIFO order.
// Why: A trading engine must process events in the order they were submitted.
//      Out-of-order delivery here would silently corrupt the event pipeline.
// -----------------------------------------------------------------------------
TEST_F(ThreadSafeQueueTest, FIFOOrder) {
  constexpr int kCount = 100;
  for (int i = 0; i < kCount; ++i) {
    queue.push(i);
  }

  for (int i = 0; i < kCount; ++i) {
    EXPECT_EQ(queue.pop(), i) << "FIFO violated at index " << i;
  }
  EXPECT_TRUE(queue.empty());
}

// -----------------------------------------------------------------------------
// 4. try_pop() on an empty queue must return std::nullopt immediately.
// Why: Non-blocking consumers (e.g. the EventLoopThread worker) rely on
//      try_pop() returning instantly so they can check the stop condition.
// -----------------------------------------------------------------------------
TEST_F(ThreadSafeQueueTest, TryPopEmptyReturnsNullopt) {
  std::optional<int> result = queue.try_pop();
  EXPECT_FALSE(result.has_value());
}

// -----------------------------------------------------------------------------
// 5. try_pop() on a non-empty queue must return the front item.
// Why: Symmetric check — try_pop must behave identically to pop() when an
//      item is available, just without blocking.
// -----------------------------------------------------------------------------
TEST_F(ThreadSafeQueueTest, TryPopNonEmpty) {
  queue.push(99);
  std::optional<int> result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 99);
  EXPECT_TRUE(queue.empty());
}

// -----------------------------------------------------------------------------
// 6. Blocking pop() must wait until another thread pushes.
// Why: This tests the condition_variable wakeup path. If notify_one() is
//      missing or the predicate is wrong, the consumer thread deadlocks.
//
// How: We spawn a consumer that calls pop(). After a short delay (to ensure
//      the consumer is blocked), the main thread pushes a value. The consumer
//      must wake up, receive the value, and exit.
// -----------------------------------------------------------------------------
TEST_F(ThreadSafeQueueTest, BlockingPopWaitsForPush) {
  std::atomic<int> received{-1};

  // Consumer thread — will block in pop() until the producer pushes.
  std::thread consumer([this, &received] { received.store(queue.pop()); });

  // Give the consumer time to enter the blocked wait inside pop().
  // 20 ms is generous; the consumer should reach the condition_variable::wait
  // within microseconds.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(received.load(), -1);  // Still blocked — nothing pushed yet.

  queue.push(77);
  consumer.join();

  EXPECT_EQ(received.load(), 77);
}

// -----------------------------------------------------------------------------
// 7. Concurrent multi-producer, multi-consumer stress test.
// Why: ThreadSafeQueue is used at thread boundaries in the engine. If the
//      mutex or condition_variable logic has a race, this test will surface it
//      as lost items or duplicate pops.
//
// How: 4 producers each push a unique range of ints. 4 consumers each pop
//      until they collectively drain all items. We verify that every pushed
//      value was popped exactly once by collecting results into a sorted set.
// -----------------------------------------------------------------------------
TEST_F(ThreadSafeQueueTest, ConcurrentPushPop) {
  constexpr int kProducers = 4;
  constexpr int kConsumers = 4;
  constexpr int kItemsPerProducer = 1000;
  constexpr int kTotalItems = kProducers * kItemsPerProducer;

  // Producers: each pushes [start, start + kItemsPerProducer).
  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([this, p] {
      int start = p * kItemsPerProducer;
      for (int i = start; i < start + kItemsPerProducer; ++i) {
        queue.push(i);
      }
    });
  }

  // Consumers: each pops items and records them. We use an atomic counter
  // to know when all items have been consumed.
  std::atomic<int> consumed{0};
  std::vector<std::vector<int>> per_consumer(kConsumers);

  std::vector<std::thread> consumers;
  for (int c = 0; c < kConsumers; ++c) {
    consumers.emplace_back([this, c, &consumed, &per_consumer] {
      while (true) {
        std::optional<int> item = queue.try_pop();
        if (item) {
          per_consumer[c].push_back(*item);
          if (consumed.fetch_add(1) + 1 == kProducers * kItemsPerProducer) {
            return;  // We took the last item.
          }
        } else if (consumed.load() >= kProducers * kItemsPerProducer) {
          return;  // All items consumed by other threads.
        }
        // Tiny yield to avoid busy-spin in the test.
        std::this_thread::yield();
      }
    });
  }

  for (auto& t : producers) t.join();
  for (auto& t : consumers) t.join();

  // Merge all consumer results and verify completeness.
  std::vector<int> all;
  for (auto& v : per_consumer) {
    all.insert(all.end(), v.begin(), v.end());
  }
  std::sort(all.begin(), all.end());

  ASSERT_EQ(static_cast<int>(all.size()), kTotalItems);

  // Every integer in [0, kTotalItems) must appear exactly once.
  for (int i = 0; i < kTotalItems; ++i) {
    EXPECT_EQ(all[i], i) << "Missing or duplicate item at index " << i;
  }
}
