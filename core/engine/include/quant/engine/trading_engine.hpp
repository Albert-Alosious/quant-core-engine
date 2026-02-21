#pragma once

#include "quant/concurrent/event_loop_thread.hpp"
#include "quant/concurrent/order_id_generator.hpp"
#include "quant/domain/risk_limits.hpp"
#include "quant/network/ipc_server.hpp"
#include "quant/network/market_data_thread.hpp"
#include "quant/network/order_routing_thread.hpp"
#include "quant/risk/i_reconciler.hpp"
#include "quant/risk/order_tracker.hpp"
#include "quant/risk/position_engine.hpp"
#include "quant/risk/risk_engine.hpp"
#include "quant/strategy/dummy_strategy.hpp"
#include "quant/time/simulation_time_provider.hpp"

#include <memory>
#include <string>

namespace quant {

// -----------------------------------------------------------------------------
// TradingEngine
// -----------------------------------------------------------------------------
//
// @brief  Central orchestrator that owns all threads, event loops, network
//         I/O threads, and engine components.
//
// @details
// Provides a clean lifecycle API (start/stop) so that main() and tests can
// use the engine without manually wiring internals.
//
// Why in architecture (architecture.md):
//   "Single executable binary" — TradingEngine is the programmatic root.
//   "No global mutable state" — engine owns everything by value or unique_ptr.
//   "Modular architecture" — components are created and destroyed through the
//   engine; adding new strategies or risk modules means extending the engine,
//   not editing main().
//
// Thread layout (Phase 4):
//
//   strategy_loop thread     → DummyStrategy callbacks (pure logic)
//   risk_loop thread         → OrderTracker + PositionEngine + RiskEngine
//   order_routing thread     → ExecutionEngine (future: broker API I/O)
//   market_data thread       → MarketDataGateway ZMQ recv loop
//
//   main thread              → engine.start(), wait for shutdown, engine.stop()
//
// Cross-thread bridges (wired in start()):
//   1. strategy_loop  →  risk_loop:       SignalEvent
//   2. risk_loop      →  order_routing:   OrderEvent
//   3. order_routing  →  risk_loop:       ExecutionReportEvent
//   4. market_data    →  strategy_loop:   MarketDataEvent (via pushEvent)
//
// Thread model:
//   TradingEngine is constructed and destroyed on the caller's thread (main).
//   start() spawns four worker threads. stop() joins all of them.
//   Between start() and stop(), the caller may push events via pushEvent()
//   from any thread. EventBus accessors allow external subscribers.
//
// Ownership:
//   TradingEngine
//    ├── order_id_gen_           (OrderIdGenerator — value member, non-movable)
//    ├── risk_limits_            (RiskLimits — value member, immutable config)
//    ├── sim_clock_              (SimulationTimeProvider& — non-owning ref)
//    ├── strategy_loop_          (EventLoopThread — value member)
//    ├── risk_loop_              (EventLoopThread — value member)
//    ├── order_routing_thread_   (unique_ptr<OrderRoutingThread>)
//    ├── market_data_thread_     (unique_ptr<MarketDataThread>)
//    ├── ipc_server_             (unique_ptr<IpcServer>)
//    ├── strategy_               (unique_ptr<DummyStrategy>)
//    ├── order_tracker_          (unique_ptr<OrderTracker>)
//    ├── position_engine_        (unique_ptr<PositionEngine>)
//    ├── risk_engine_            (unique_ptr<RiskEngine>)
//    └── reconciler              (IReconciler* — optional, non-owning)
//
// Components are heap-allocated (unique_ptr) so we can control destruction
// order: components must be destroyed before the loops they reference.
// EventLoopThreads are value members destroyed last (reverse member order).
// The OrderIdGenerator is a value member that outlives all components.
// -----------------------------------------------------------------------------
class TradingEngine {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  //
  // @brief  Creates the engine, binding it to the given simulation clock.
  //
  // @param  sim_clock        Reference to the SimulationTimeProvider that
  //                          will drive simulated time for the
  //                          MarketDataGateway and MockExecutionEngine.
  //                          Must outlive this engine.
  // @param  market_data_endpoint  ZMQ endpoint for market data. If empty,
  //                               MarketDataThread is not created (useful
  //                               for unit tests that push events manually
  //                               via pushMarketData()). Defaults to
  //                               "tcp://127.0.0.1:5555".
  // @param  ipc_cmd_endpoint      ZMQ endpoint for the IPC command (REP)
  //                               socket. If empty, IpcServer is not
  //                               created. Defaults to port 5556.
  // @param  ipc_pub_endpoint      ZMQ endpoint for the IPC telemetry (PUB)
  //                               socket. If empty, IpcServer is not
  //                               created. Defaults to port 5557.
  //
  // @details
  // No threads are spawned and no sockets are opened in the constructor.
  // Call start() to bring the engine to a running state.
  //
  // Thread-safety: Safe to construct from main().
  // Side-effects:  None.
  // -------------------------------------------------------------------------
  explicit TradingEngine(
      SimulationTimeProvider& sim_clock,
      std::string market_data_endpoint = "tcp://127.0.0.1:5555",
      std::string ipc_cmd_endpoint = "tcp://127.0.0.1:5556",
      std::string ipc_pub_endpoint = "tcp://127.0.0.1:5557");

  // Destructor calls stop() for RAII safety.
  ~TradingEngine();

  // Non-copyable, non-movable: owns threads and non-movable members.
  TradingEngine(const TradingEngine&) = delete;
  TradingEngine& operator=(const TradingEngine&) = delete;
  TradingEngine(TradingEngine&&) = delete;
  TradingEngine& operator=(TradingEngine&&) = delete;

  // -------------------------------------------------------------------------
  // start(reconciler)
  // -------------------------------------------------------------------------
  //
  // @brief  Brings the engine to a running state, optionally reconciling
  //         exchange state before processing any events.
  //
  // @param  reconciler  Optional non-owning pointer to an IReconciler. If
  //                     non-null, the synchronization gate runs before event
  //                     loop threads are spawned. If null (default), the gate
  //                     is skipped.
  //
  // @details
  // Startup sequence:
  //   1. Create stateful components (OrderTracker, PositionEngine).
  //   2. Synchronization Gate (if reconciler != nullptr).
  //   3. Start core event loops (strategy_loop, risk_loop).
  //   4. Wire cross-thread bridges (SignalEvent, OrderEvent,
  //      ExecutionReportEvent).
  //   5. Start OrderRoutingThread (ExecutionEngine on its own thread).
  //   6. Create remaining components (DummyStrategy, RiskEngine).
  //   7. Start MarketDataThread (begins receiving ticks LAST).
  //
  // Market data starts last so all subscribers are live before any tick
  // enters the pipeline.
  //
  // Idempotent: calling start() on an already-running engine does nothing.
  //
  // Thread-safety: Call from one thread only (main).
  // Side-effects:  Spawns four worker threads. May invoke reconciler I/O.
  //                Opens a ZMQ socket.
  // -------------------------------------------------------------------------
  void start(IReconciler* reconciler = nullptr);

  // -------------------------------------------------------------------------
  // stop()
  // -------------------------------------------------------------------------
  //
  // @brief  Shuts down all threads and destroys all components.
  //
  // @details
  // Shutdown sequence:
  //   1. Stop MarketDataThread (no new ticks enter the pipeline).
  //   2. Destroy logic components (strategy, risk_engine, position_engine,
  //      order_tracker — they unsubscribe from their buses).
  //   3. Stop OrderRoutingThread (destroys ExecutionEngine, joins thread).
  //   4. Stop core event loops (strategy_loop, risk_loop — joins threads).
  //
  // After stop(), the engine is inactive. start() may be called again.
  //
  // Thread-safety: Call from one thread only (main). Idempotent.
  // -------------------------------------------------------------------------
  void stop();

  // -------------------------------------------------------------------------
  // pushMarketData(event)
  // -------------------------------------------------------------------------
  //
  // @brief  Enqueues a MarketDataEvent into the strategy loop's queue.
  //
  // @details
  // Convenience method for tests and manual injection. In production mode,
  // MarketDataThread pushes events via pushEvent() automatically.
  //
  // Thread-safety: Safe to call from any thread.
  // -------------------------------------------------------------------------
  void pushMarketData(MarketDataEvent event);

  // -------------------------------------------------------------------------
  // pushEvent(event)
  // -------------------------------------------------------------------------
  //
  // @brief  Enqueues a generic Event into the strategy loop's queue.
  //
  // @param  event  The Event variant to enqueue. Moved into the queue.
  //
  // @details
  // This is the event sink bound to MarketDataThread's gateway. Also
  // usable by tests for arbitrary event injection.
  //
  // Thread-safety: Safe to call from any thread.
  // -------------------------------------------------------------------------
  void pushEvent(Event event);

  // -------------------------------------------------------------------------
  // executeCommand(cmd)
  // -------------------------------------------------------------------------
  //
  // @brief  Processes a command string from the IPC server and returns a
  //         JSON response.
  //
  // @param  cmd  The command string received on the REP socket (e.g.,
  //              "PING", "STATUS", "HALT").
  //
  // @return JSON-formatted response string.
  //
  // @details
  // Supported commands:
  //   "PING"   → {"status":"ok","response":"PONG"}
  //   "STATUS" → {"status":"ok","halted":bool,"positions":[...]}
  //   "HALT"   → {"status":"ok","response":"Trading halted"}
  //   other    → {"status":"error","response":"Unknown command: ..."}
  //
  // Thread model: Called on the IPC server thread. Accesses PositionEngine
  //               via getSnapshots() (shared_lock) and RiskEngine via
  //               isHalted()/haltTrading() (atomic). Both are thread-safe.
  //
  // Thread-safety: Safe to call from any thread.
  // Side-effects:  "HALT" activates the RiskEngine kill switch.
  // -------------------------------------------------------------------------
  std::string executeCommand(const std::string& cmd);

  // -------------------------------------------------------------------------
  // strategyEventBus() / riskExecutionEventBus()
  // -------------------------------------------------------------------------
  //
  // @brief  Provide access to core event buses for external subscribers.
  //
  // Thread-safety: EventBus::subscribe() is thread-safe.
  // -------------------------------------------------------------------------
  EventBus& strategyEventBus();
  EventBus& riskExecutionEventBus();

 private:
  // --- Non-owning reference to the simulation clock -------------------------
  SimulationTimeProvider& sim_clock_;

  // --- Network endpoints (empty = disabled) ----------------------------------
  std::string market_data_endpoint_;
  std::string ipc_cmd_endpoint_;
  std::string ipc_pub_endpoint_;

  // --- ID generator (value member — outlives all components) ----------------
  OrderIdGenerator order_id_gen_;

  // --- Risk limits (value member — immutable engine-wide config) ------------
  domain::RiskLimits risk_limits_;

  // --- Core event loops (value members — destroyed last) --------------------
  EventLoopThread strategy_loop_;
  EventLoopThread risk_loop_;

  // --- Network I/O threads (heap-allocated for controlled lifecycle) ---------
  std::unique_ptr<OrderRoutingThread> order_routing_thread_;
  std::unique_ptr<MarketDataThread> market_data_thread_;
  std::unique_ptr<IpcServer> ipc_server_;

  // --- Logic components (heap-allocated for controlled destruction order) ----
  std::unique_ptr<DummyStrategy> strategy_;
  std::unique_ptr<OrderTracker> order_tracker_;
  std::unique_ptr<PositionEngine> position_engine_;
  std::unique_ptr<RiskEngine> risk_engine_;

  bool running_{false};
};

}  // namespace quant
