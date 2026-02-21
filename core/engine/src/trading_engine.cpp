#include "quant/engine/trading_engine.hpp"

#include <iostream>
#include <utility>

namespace quant {

// -----------------------------------------------------------------------------
// Destructor: RAII stop
// -----------------------------------------------------------------------------
// If the caller forgets to call stop(), this ensures threads are joined and
// components are destroyed in the correct order. Safe to call even if stop()
// was already called (idempotent).
// -----------------------------------------------------------------------------
TradingEngine::~TradingEngine() { stop(); }

// -----------------------------------------------------------------------------
// start()
// -----------------------------------------------------------------------------
void TradingEngine::start(IReconciler* reconciler) {
  if (running_) {
    return;  // Idempotent: already running.
  }

  // ---  1) Create stateful components FIRST (before event loops start) ------
  // OrderTracker and PositionEngine subscribe to their EventBus in their
  // constructors. The EventBus is a value member of EventLoopThread and
  // exists even before the loop's thread is spawned, so subscribe() is safe.
  //
  // These components must exist before the synchronization gate so we can
  // call hydratePosition() and hydrateOrder() on the main thread.
  //
  // Subscriber ordering is preserved: OrderTracker subscribes first, then
  // PositionEngine — matching the required callback invocation order.
  order_tracker_ =
      std::make_unique<OrderTracker>(risk_execution_loop_.eventBus());
  position_engine_ =
      std::make_unique<PositionEngine>(risk_execution_loop_.eventBus());

  // ---  2) Synchronization Gate (optional) ----------------------------------
  // If a reconciler is provided, query the exchange for pre-existing state
  // and inject it into the stateful components. This runs on the main thread
  // before any event loop thread is spawned — no race conditions.
  if (reconciler != nullptr) {
    auto positions = reconciler->reconcilePositions();
    for (const auto& pos : positions) {
      position_engine_->hydratePosition(pos);
    }

    auto orders = reconciler->reconcileOrders();
    for (const auto& order : orders) {
      order_tracker_->hydrateOrder(order);
    }

    std::cout << "[TradingEngine] Reconciliation complete: "
              << positions.size() << " position(s), "
              << orders.size() << " open order(s) hydrated.\n";
  }

  // ---  3) Start both event loops (spawns worker threads) -------------------
  strategy_loop_.start();
  risk_execution_loop_.start();

  // ---  4) Wire the cross-thread forwarder ----------------------------------
  // SignalEvent published on the strategy_loop bus is pushed into the
  // risk_execution_loop queue. This is the only place where events cross
  // the thread boundary. The forwarder runs on the strategy_loop thread.
  strategy_loop_.eventBus().subscribe<SignalEvent>(
      [this](const SignalEvent& e) { risk_execution_loop_.push(e); });

  // ---  5) Create remaining components --------------------------------------
  // Components subscribe to their loop's EventBus in their constructors.
  // Creation order determines callback invocation order for the same event
  // type, because EventBus stores subscribers in registration order.
  //
  // Required ordering on the risk_execution_loop:
  //   1. OrderTracker FIRST (already created in step 1)
  //   2. PositionEngine (already created in step 1)
  //   3. RiskEngine — subscribes to SignalEvent, publishes OrderEvent.
  //   4. ExecutionEngine — subscribes to OrderEvent, publishes
  //      ExecutionReportEvent (Accepted then Filled). OrderTracker's and
  //      PositionEngine's OrderEvent callbacks have already fired (they
  //      were registered first), so their caches are warm when the
  //      execution reports arrive.
  //
  // DummyStrategy lives on the strategy_loop, so its creation order
  // relative to risk_execution_loop components does not matter.
  strategy_ = std::make_unique<DummyStrategy>(strategy_loop_.eventBus());
  risk_engine_ =
      std::make_unique<RiskEngine>(risk_execution_loop_.eventBus(),
                                   order_id_gen_);
  execution_engine_ =
      std::make_unique<ExecutionEngine>(risk_execution_loop_.eventBus());

  running_ = true;

  std::cout << "[TradingEngine] started. Strategy and Risk/Execution loops "
               "are running.\n";
}

// -----------------------------------------------------------------------------
// stop()
// -----------------------------------------------------------------------------
void TradingEngine::stop() {
  if (!running_) {
    return;  // Idempotent: already stopped (or never started).
  }

  // ---  1) Destroy components first -----------------------------------------
  // Components unsubscribe from the bus in their destructors. This must happen
  // before we stop the loops, otherwise a callback could fire on a destroyed
  // component. Reverse creation order:
  //   Created: strategy, order_tracker, position_engine, risk_engine, execution_engine
  //   Destroy: execution_engine, risk_engine, position_engine, order_tracker, strategy
  execution_engine_.reset();
  risk_engine_.reset();
  position_engine_.reset();
  order_tracker_.reset();
  strategy_.reset();

  // ---  2) Stop event loops (joins worker threads) --------------------------
  // After this, no worker threads are running. Events remaining in the queue
  // are discarded.
  strategy_loop_.stop();
  risk_execution_loop_.stop();

  running_ = false;

  std::cout << "[TradingEngine] stopped. All threads joined.\n";
}

// -----------------------------------------------------------------------------
// pushMarketData(event)
// -----------------------------------------------------------------------------
void TradingEngine::pushMarketData(MarketDataEvent event) {
  strategy_loop_.push(std::move(event));
}

// -----------------------------------------------------------------------------
// pushEvent(event)
// -----------------------------------------------------------------------------
// Accepts the full Event variant and enqueues it on the strategy loop.
// Used as the event_sink callback by MarketDataGateway. The Event is moved
// into the queue to avoid unnecessary copies of the variant.
// -----------------------------------------------------------------------------
void TradingEngine::pushEvent(Event event) {
  strategy_loop_.push(std::move(event));
}

// -----------------------------------------------------------------------------
// EventBus accessors
// -----------------------------------------------------------------------------
EventBus& TradingEngine::strategyEventBus() {
  return strategy_loop_.eventBus();
}

EventBus& TradingEngine::riskExecutionEventBus() {
  return risk_execution_loop_.eventBus();
}

}  // namespace quant
