#pragma once

#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event_types.hpp"

namespace quant {

// -----------------------------------------------------------------------------
// DummyStrategy
// -----------------------------------------------------------------------------
// Responsibility: Minimal strategy that subscribes to MarketDataEvent and
// publishes SignalEvent when a simple condition is met. Used to test the
// pipeline from strategy thread to risk/execution thread.
//
// Why in architecture: Demonstrates that strategy only emits events (never
// calls execution). Fits the Strategy Thread in the threading model; events
// are forwarded to risk/execution via a separate subscriber that pushes to
// risk_execution_loop.
//
// Thread model: Callbacks run on whatever thread publishes to the bus this
// strategy is attached to (typically the strategy_loop thread). Must be
// registered with the strategy_loop's EventBus and receive MarketDataEvent
// on that same bus.
// -----------------------------------------------------------------------------
class DummyStrategy {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // What: Subscribes to MarketDataEvent on the given bus. When a tick meets
  // the condition (e.g. price above threshold), publishes a SignalEvent.
  // Why: Strategy must be attached to the strategy_loop's bus so it runs
  // on the strategy thread. No global state—bus is injected.
  // Thread-safety: Safe to call from one thread; bus.subscribe() is
  // thread-safe. Callback will run on the loop thread that publishes.
  // Input: bus — EventBus to subscribe to and publish to (strategy_loop's).
  // Output: None.
  // -------------------------------------------------------------------------
  explicit DummyStrategy(EventBus& bus);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // What: Unsubscribes from the bus so no further callbacks run.
  // Why: RAII; avoid use-after-free if the bus outlives the strategy or
  // continues publishing after we are destroyed.
  // Thread-safety: Safe to call from any thread; bus.unsubscribe() is
  // thread-safe. If a callback is in progress, it may still run for the
  // current event; it will not run for subsequent events.
  // -------------------------------------------------------------------------
  ~DummyStrategy();

  // Non-copyable: holds a subscription id and bus reference; copying would
  // duplicate or invalidate the subscription.
  DummyStrategy(const DummyStrategy&) = delete;
  DummyStrategy& operator=(const DummyStrategy&) = delete;

  // Non-movable: moving would leave the subscription attached to the old
  // object; the new object would have an invalid id. We could implement
  // move with unsubscribe in the moved-from object, but for minimal we
  // delete it.
  DummyStrategy(DummyStrategy&&) = delete;
  DummyStrategy& operator=(DummyStrategy&&) = delete;

 private:
  // -------------------------------------------------------------------------
  // onMarketData(event)
  // -------------------------------------------------------------------------
  // What: Called when a MarketDataEvent is published. If the simple condition
  // is met (e.g. price > threshold), builds and publishes a SignalEvent.
  // Why: This is the strategy logic—convert market data into a signal.
  // Thread-safety: Runs on the strategy loop thread only; no shared mutable
  // state. Must not block or call into execution directly.
  // -------------------------------------------------------------------------
  void onMarketData(const MarketDataEvent& event);

  EventBus& bus_;
  EventBus::SubscriptionId subscription_id_{0};

  // Simple condition: emit signal when price is above this threshold.
  // Kept minimal for testing; a real strategy would use more logic.
  static constexpr double kPriceThreshold = 0.0;

  // Strategy id used in emitted SignalEvent so the risk layer can attribute
  // the signal. Fixed for this dummy; could be configurable.
  static constexpr const char* kStrategyId = "DummyStrategy";
};

}  // namespace quant
