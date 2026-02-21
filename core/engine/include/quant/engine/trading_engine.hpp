#pragma once

#include "quant/concurrent/event_loop_thread.hpp"
#include "quant/execution/execution_engine.hpp"
#include "quant/risk/position_engine.hpp"
#include "quant/risk/risk_engine.hpp"
#include "quant/strategy/dummy_strategy.hpp"

#include <memory>

namespace quant {

// -----------------------------------------------------------------------------
// TradingEngine
// -----------------------------------------------------------------------------
// Responsibility: Central orchestrator that owns all threads, event loops,
// and engine components. Provides a clean lifecycle API (start/stop) so that
// main() and tests can use the engine without manually wiring internals.
//
// Why in architecture (architecture.md):
// - "Single executable binary" — TradingEngine is the programmatic root.
// - "No global mutable state" — engine owns everything by value or unique_ptr.
// - "Modular architecture" — components are created and destroyed through the
//   engine; adding new strategies or risk modules means extending the engine,
//   not editing main().
//
// Thread model:
// - TradingEngine is constructed and destroyed on the caller's thread (main).
// - start() spawns worker threads inside the EventLoopThreads.
// - stop() joins those threads. After stop(), no worker threads are running.
// - Between start() and stop(), the caller may push events via pushMarketData()
//   from any thread. EventBus accessors allow external subscribers (e.g. for
//   logging or monitoring) — subscribe before or after start().
//
// Ownership:
//   TradingEngine
//    ├── strategy_loop_         (EventLoopThread — value member)
//    ├── risk_execution_loop_   (EventLoopThread — value member)
//    ├── strategy_              (unique_ptr<DummyStrategy>)
//    ├── risk_engine_           (unique_ptr<RiskEngine>)
//    ├── execution_engine_      (unique_ptr<ExecutionEngine>)
//    └── position_engine_       (unique_ptr<PositionEngine>)
//
// Components are heap-allocated (unique_ptr) so we can control destruction
// order: components must be destroyed before the loops they reference.
// EventLoopThreads are value members destroyed last (reverse member order).
// -----------------------------------------------------------------------------
class TradingEngine {
 public:
  TradingEngine() = default;

  // Destructor calls stop() for RAII safety. If the caller forgets to stop(),
  // threads are still joined cleanly.
  ~TradingEngine();

  // Non-copyable, non-movable: owns threads and non-movable members.
  TradingEngine(const TradingEngine&) = delete;
  TradingEngine& operator=(const TradingEngine&) = delete;
  TradingEngine(TradingEngine&&) = delete;
  TradingEngine& operator=(TradingEngine&&) = delete;

  // -------------------------------------------------------------------------
  // start()
  // -------------------------------------------------------------------------
  // What: Starts both event loops, wires the cross-thread forwarder, and
  // creates all components (DummyStrategy, RiskEngine, ExecutionEngine).
  // Why: Single call to bring the engine to a running state. Idempotent —
  // calling start() on an already-running engine does nothing.
  // Thread-safety: Call from one thread only (main). Not meant for concurrent
  // calls.
  // -------------------------------------------------------------------------
  void start();

  // -------------------------------------------------------------------------
  // stop()
  // -------------------------------------------------------------------------
  // What: Destroys components (they unsubscribe from the bus), then stops
  // both event loops (joins threads). After stop(), the engine is inactive.
  // start() may be called again to restart.
  // Why: Clean shutdown. Components are destroyed before loops so no callbacks
  // fire into dead objects.
  // Thread-safety: Call from one thread only (main). Idempotent.
  // -------------------------------------------------------------------------
  void stop();

  // -------------------------------------------------------------------------
  // pushMarketData(event)
  // -------------------------------------------------------------------------
  // What: Enqueues a MarketDataEvent into the strategy loop's queue.
  // Why: This is the external entry point for market data. In the future,
  // a MarketDataFeed component will call this; for now, main() or tests do.
  // Thread-safety: Safe to call from any thread (delegates to push()).
  // -------------------------------------------------------------------------
  void pushMarketData(MarketDataEvent event);

  // -------------------------------------------------------------------------
  // pushEvent(event)
  // -------------------------------------------------------------------------
  // @brief  Enqueues a generic Event (std::variant) into the strategy loop's
  //         queue for processing on the strategy thread.
  //
  // @param  event  The Event variant to enqueue. Moved into the queue.
  //
  // @details
  // This is the entry point used by MarketDataGateway's event_sink callback.
  // The gateway constructs a MarketDataEvent, wraps it in the Event variant,
  // and calls event_sink_(event). TradingEngine binds event_sink to this
  // method so events flow into the strategy loop without the gateway knowing
  // about EventLoopThread.
  //
  // Why a separate method from pushMarketData():
  //   pushMarketData() accepts MarketDataEvent directly — convenient for
  //   tests and manual injection. pushEvent() accepts the full Event variant
  //   — required by the MarketDataGateway::EventSink signature
  //   (std::function<void(Event)>). Both ultimately call
  //   strategy_loop_.push().
  //
  // Thread-safety: Safe to call from any thread. Delegates to
  //                EventLoopThread::push(), which is thread-safe.
  // Side-effects:  Enqueues the event; the strategy loop thread will
  //                eventually pop and publish it.
  // -------------------------------------------------------------------------
  void pushEvent(Event event);

  // -------------------------------------------------------------------------
  // strategyEventBus() / riskExecutionEventBus()
  // -------------------------------------------------------------------------
  // What: Provide access to the event buses for external subscribers (e.g.
  // logging, monitoring). Subscribers registered here run on the respective
  // loop thread.
  // Why: Allows main() or monitoring code to observe events without being
  // part of the engine internals.
  // Thread-safety: EventBus::subscribe() is thread-safe; safe to call from
  // any thread.
  // -------------------------------------------------------------------------
  EventBus& strategyEventBus();
  EventBus& riskExecutionEventBus();

 private:
  // --- Event loops (value members — destroyed last in reverse order) --------
  EventLoopThread strategy_loop_;
  EventLoopThread risk_execution_loop_;

  // --- Components (heap-allocated for controlled destruction order) ----------
  std::unique_ptr<DummyStrategy> strategy_;
  std::unique_ptr<RiskEngine> risk_engine_;
  std::unique_ptr<ExecutionEngine> execution_engine_;
  std::unique_ptr<PositionEngine> position_engine_;

  // Tracks whether start() has been called (and stop() has not).
  bool running_{false};
};

}  // namespace quant
