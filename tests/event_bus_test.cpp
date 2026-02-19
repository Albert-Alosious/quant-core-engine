// =============================================================================
// event_bus_test.cpp
// =============================================================================
// Unit tests for quant::EventBus.
//
// Validates:
//   - Generic (all-event) subscription receives every event type
//   - Typed subscription receives only the matching event type
//   - Multiple subscribers all receive the same published event
//   - Unsubscribe correctly stops delivery
//   - Edge cases: unsubscribe unknown id, publish to empty bus
//   - Re-entrant publish (subscriber publishes inside callback) — no deadlock
//   - Data integrity through the variant dispatch path
//
// Design note: All tests are single-threaded (testing EventBus in isolation).
// Cross-thread delivery is covered in pipeline_integration_test.cpp.
// =============================================================================

#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/execution_report_event.hpp"
#include "quant/events/order_event.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// =============================================================================
// Test fixture: provides a fresh EventBus for each test.
// =============================================================================
class EventBusTest : public ::testing::Test {
 protected:
  quant::EventBus bus;

  // Convenience: build a MarketDataEvent with a given symbol and price.
  static quant::MarketDataEvent makeMD(const std::string& symbol,
                                        double price) {
    quant::MarketDataEvent e;
    e.symbol = symbol;
    e.price = price;
    e.quantity = 1.0;
    e.sequence_id = 0;
    return e;
  }

  // Convenience: build a SignalEvent.
  static quant::SignalEvent makeSignal(const std::string& strategy,
                                        const std::string& symbol) {
    quant::SignalEvent e;
    e.strategy_id = strategy;
    e.symbol = symbol;
    e.side = quant::SignalEvent::Side::Buy;
    e.strength = 1.0;
    e.sequence_id = 0;
    return e;
  }
};

// -----------------------------------------------------------------------------
// 1. A generic subscriber must be invoked for every event type.
// Why: Components like a logger subscribe generically and must see every event.
//      If the variant dispatch skips a type, logs would be silently incomplete.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, GenericSubscriberReceivesAllEvents) {
  int call_count = 0;
  bus.subscribe([&call_count](const quant::Event&) { ++call_count; });

  bus.publish(makeMD("AAPL", 150.0));
  bus.publish(makeSignal("strat1", "AAPL"));
  bus.publish(quant::HeartbeatEvent{"engine", "ok"});

  EXPECT_EQ(call_count, 3);
}

// -----------------------------------------------------------------------------
// 2. A typed subscriber must fire only for its registered event type.
// Why: The typed subscribe<T> is the primary API for strategies, risk, etc.
//      If it fires for the wrong type, a RiskEngine would try to process a
//      HeartbeatEvent as a SignalEvent — corrupting the order pipeline.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, TypedSubscriberFiltersCorrectly) {
  int md_count = 0;
  bus.subscribe<quant::MarketDataEvent>(
      [&md_count](const quant::MarketDataEvent&) { ++md_count; });

  // Publish one MarketDataEvent and one SignalEvent.
  bus.publish(makeMD("AAPL", 150.0));
  bus.publish(makeSignal("strat1", "AAPL"));

  // Only the MarketDataEvent should have been delivered.
  EXPECT_EQ(md_count, 1);
}

// -----------------------------------------------------------------------------
// 3. Multiple subscribers must all receive the same published event.
// Why: DummyStrategy and a logging subscriber might both subscribe to
//      MarketDataEvent. If only one receives the event, the other is blind.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, MultipleSubscribersAllReceive) {
  int count_a = 0;
  int count_b = 0;
  int count_c = 0;

  bus.subscribe<quant::MarketDataEvent>(
      [&count_a](const quant::MarketDataEvent&) { ++count_a; });
  bus.subscribe<quant::MarketDataEvent>(
      [&count_b](const quant::MarketDataEvent&) { ++count_b; });
  bus.subscribe<quant::MarketDataEvent>(
      [&count_c](const quant::MarketDataEvent&) { ++count_c; });

  bus.publish(makeMD("AAPL", 150.0));

  EXPECT_EQ(count_a, 1);
  EXPECT_EQ(count_b, 1);
  EXPECT_EQ(count_c, 1);
}

// -----------------------------------------------------------------------------
// 4. After unsubscribe(id), the callback must not fire for future publishes.
// Why: Components unsubscribe during shutdown (RAII destructors). If callbacks
//      still fire after unsubscribe, we get use-after-free on captured state.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, UnsubscribeStopsDelivery) {
  int call_count = 0;
  auto id = bus.subscribe<quant::MarketDataEvent>(
      [&call_count](const quant::MarketDataEvent&) { ++call_count; });

  bus.publish(makeMD("AAPL", 150.0));
  EXPECT_EQ(call_count, 1);  // Should have received the first event.

  bus.unsubscribe(id);

  bus.publish(makeMD("AAPL", 151.0));
  EXPECT_EQ(call_count, 1);  // Must NOT have received the second event.
}

// -----------------------------------------------------------------------------
// 5. Unsubscribing a non-existent id must not crash or throw.
// Why: Defensive programming — if destruction order causes a double-unsubscribe
//      or an unsubscribe with a stale id, the engine must not segfault.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, UnsubscribeNonExistentIdIsNoOp) {
  // Bus has no subscribers. Unsubscribing a bogus id should be harmless.
  EXPECT_NO_FATAL_FAILURE(bus.unsubscribe(9999));
}

// -----------------------------------------------------------------------------
// 6. Publishing to a bus with zero subscribers must not crash.
// Why: During startup/shutdown, the bus may temporarily have no subscribers.
//      A crash here would bring down the entire engine process.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, PublishWithNoSubscribers) {
  EXPECT_NO_FATAL_FAILURE(bus.publish(makeMD("AAPL", 150.0)));
}

// -----------------------------------------------------------------------------
// 7. A subscriber that calls publish() inside its callback must not deadlock.
// Why: The EventBus copies the subscriber list before invoking callbacks, so
//      publish() inside a callback takes a new lock on mutex_ without blocking
//      (the outer publish has already released the lock). If the implementation
//      held the lock across callbacks, this test would hang forever.
//
// Scenario: subscriber A receives MarketDataEvent and publishes a SignalEvent.
//           Subscriber B receives the SignalEvent.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, SubscriberCanPublishInsideCallback) {
  int signal_received = 0;

  // Subscriber B: receives SignalEvent.
  bus.subscribe<quant::SignalEvent>(
      [&signal_received](const quant::SignalEvent&) { ++signal_received; });

  // Subscriber A: receives MarketDataEvent, publishes SignalEvent.
  bus.subscribe<quant::MarketDataEvent>(
      [this](const quant::MarketDataEvent& md) {
        quant::SignalEvent signal;
        signal.strategy_id = "reentrant";
        signal.symbol = md.symbol;
        signal.side = quant::SignalEvent::Side::Buy;
        signal.strength = 1.0;
        bus.publish(signal);
      });

  bus.publish(makeMD("AAPL", 150.0));

  // SignalEvent should have been delivered to subscriber B.
  EXPECT_EQ(signal_received, 1);
}

// -----------------------------------------------------------------------------
// 8. Field values must survive the variant round trip: publish → dispatch.
// Why: If the variant copy or std::get_if introduces data corruption (e.g.
//      slicing, wrong alignment), event payloads would be silently wrong.
//      In a trading engine, a corrupted price or symbol = real money lost.
// -----------------------------------------------------------------------------
TEST_F(EventBusTest, TypedSubscriberReceivesCorrectData) {
  std::string received_symbol;
  double received_price = 0.0;

  bus.subscribe<quant::MarketDataEvent>(
      [&received_symbol, &received_price](const quant::MarketDataEvent& e) {
        received_symbol = e.symbol;
        received_price = e.price;
      });

  bus.publish(makeMD("TSLA", 237.50));

  EXPECT_EQ(received_symbol, "TSLA");
  EXPECT_DOUBLE_EQ(received_price, 237.50);
}
