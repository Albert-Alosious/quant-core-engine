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
void TradingEngine::start() {
  if (running_) {
    return;  // Idempotent: already running.
  }

  // ---  1) Start both event loops (spawns worker threads) -------------------
  strategy_loop_.start();
  risk_execution_loop_.start();

  // ---  2) Wire the cross-thread forwarder ----------------------------------
  // SignalEvent published on the strategy_loop bus is pushed into the
  // risk_execution_loop queue. This is the only place where events cross
  // the thread boundary. The forwarder runs on the strategy_loop thread.
  strategy_loop_.eventBus().subscribe<SignalEvent>(
      [this](const SignalEvent& e) { risk_execution_loop_.push(e); });

  // ---  3) Create components ------------------------------------------------
  // Components subscribe to their loop's EventBus in their constructors.
  // Order matters:
  //   - DummyStrategy subscribes to MarketDataEvent on strategy_loop.
  //   - RiskEngine subscribes to SignalEvent on risk_execution_loop.
  //   - ExecutionEngine subscribes to OrderEvent on risk_execution_loop.
  // This matches the event pipeline order.
  strategy_ = std::make_unique<DummyStrategy>(strategy_loop_.eventBus());
  risk_engine_ =
      std::make_unique<RiskEngine>(risk_execution_loop_.eventBus());
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
  // component. Reverse creation order for symmetry.
  execution_engine_.reset();
  risk_engine_.reset();
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
