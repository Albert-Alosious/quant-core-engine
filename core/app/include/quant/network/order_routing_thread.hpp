#pragma once

#include "quant/concurrent/event_loop_thread.hpp"
#include "quant/execution/i_execution_engine.hpp"
#include "quant/time/i_time_provider.hpp"

#include <memory>

namespace quant {

// -----------------------------------------------------------------------------
// OrderRoutingThread — dedicated I/O thread for order execution
// -----------------------------------------------------------------------------
//
// @brief  Encapsulates an EventLoopThread and an IExecutionEngine, isolating
//         execution-layer I/O from the risk engine's critical path.
//
// @details
// Before Phase 4, the ExecutionEngine lived on the risk_execution_loop
// thread alongside RiskEngine, PositionEngine, and OrderTracker. This meant
// any latency in the execution layer (future broker API calls, network
// round-trips) would block the risk engine's event processing.
//
// OrderRoutingThread provides a separate EventLoopThread with its own queue
// and EventBus. OrderEvents are forwarded from the risk loop into this
// thread's queue. The ExecutionEngine subscribes on this thread's bus and
// publishes ExecutionReportEvents back. A reverse bridge forwards those
// reports back to the risk loop for processing by OrderTracker and
// PositionEngine.
//
// Cross-thread event bridges (wired by TradingEngine):
//
//   risk_execution_loop                order_routing_thread
//   ─────────────────────              ─────────────────────
//   RiskEngine publishes
//   OrderEvent on risk bus
//         │
//         ├── bridge subscriber ──push()──▶ order_routing_loop queue
//         │                                       │
//         │                              ExecutionEngine::onOrder()
//         │                                       │
//         │                              Publish ExecutionReportEvent
//         │                              on order_routing bus
//         │                                       │
//         ◀──push()── bridge subscriber ──────────┘
//         │
//   OrderTracker::onExecutionReport()
//   PositionEngine::onFill()
//
// Execution engine selection:
//   If a non-null ITimeProvider is passed, a MockExecutionEngine is created
//   (deterministic backtesting). If null, a live ExecutionEngine is created
//   (system_clock timestamps). This mirrors the Phase 2 design but moves
//   the selection into the thread wrapper.
//
// Thread model:
//   Constructed and destroyed on the main thread (via TradingEngine).
//   The internal EventLoopThread spawns a worker that processes events.
//   start() and stop() are called from main().
//
// Ownership:
//   Owned by TradingEngine via std::unique_ptr.
//   Owns the EventLoopThread (value member) and IExecutionEngine (unique_ptr).
//   Holds a const pointer to ITimeProvider (nullable, non-owning).
// -----------------------------------------------------------------------------
class OrderRoutingThread {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  //
  // @brief  Stores the time provider for deferred engine creation.
  //
  // @param  time_provider  If non-null, a MockExecutionEngine is created in
  //                        start(). If null, a live ExecutionEngine is
  //                        created. The pointed-to object must outlive this
  //                        OrderRoutingThread.
  //
  // @details
  // The EventLoopThread is a value member (constructed here) but its
  // worker thread is NOT started until start() is called.
  //
  // Thread-safety: Safe to construct from main().
  // Side-effects:  None — no threads spawned, no engine created.
  // -------------------------------------------------------------------------
  explicit OrderRoutingThread(const ITimeProvider* time_provider = nullptr);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  //
  // @brief  RAII: calls stop() if still running.
  // -------------------------------------------------------------------------
  ~OrderRoutingThread();

  OrderRoutingThread(const OrderRoutingThread&) = delete;
  OrderRoutingThread& operator=(const OrderRoutingThread&) = delete;
  OrderRoutingThread(OrderRoutingThread&&) = delete;
  OrderRoutingThread& operator=(OrderRoutingThread&&) = delete;

  // -------------------------------------------------------------------------
  // start()
  // -------------------------------------------------------------------------
  //
  // @brief  Starts the internal EventLoopThread and creates the execution
  //         engine.
  //
  // @details
  // 1. Starts the EventLoopThread (spawns worker thread).
  // 2. Creates the appropriate IExecutionEngine on the loop's bus.
  //
  // Idempotent: calling start() when already running is a no-op.
  //
  // Thread-safety: Call from the owning thread only (main).
  // Side-effects:  Spawns a thread, subscribes engine to the bus.
  // -------------------------------------------------------------------------
  void start();

  // -------------------------------------------------------------------------
  // stop()
  // -------------------------------------------------------------------------
  //
  // @brief  Destroys the execution engine and stops the loop thread.
  //
  // @details
  // 1. Destroys the execution engine (unsubscribes from bus).
  // 2. Stops the EventLoopThread (joins worker thread).
  //
  // Idempotent: safe to call multiple times.
  //
  // Thread-safety: Call from the owning thread only (main).
  // Side-effects:  Blocks until the worker thread exits.
  // -------------------------------------------------------------------------
  void stop();

  // -------------------------------------------------------------------------
  // push(event)
  // -------------------------------------------------------------------------
  //
  // @brief  Enqueues an event into this thread's queue for processing.
  //
  // @param  event  The event to enqueue (typically an OrderEvent forwarded
  //                from the risk loop).
  //
  // @details
  // Delegates to EventLoopThread::push(). The worker thread will pop the
  // event and publish it on this thread's bus, where the ExecutionEngine
  // is subscribed.
  //
  // Thread-safety: Safe to call from any thread.
  // Side-effects:  Enqueues the event.
  // -------------------------------------------------------------------------
  void push(Event event);

  // -------------------------------------------------------------------------
  // eventBus()
  // -------------------------------------------------------------------------
  //
  // @brief  Returns a reference to this thread's EventBus.
  //
  // @return EventBus& — valid for the lifetime of this OrderRoutingThread.
  //
  // @details
  // Used by TradingEngine to subscribe a bridge that forwards
  // ExecutionReportEvents back to the risk loop.
  //
  // Thread-safety: EventBus::subscribe() is thread-safe.
  // -------------------------------------------------------------------------
  EventBus& eventBus();

 private:
  const ITimeProvider* time_provider_;
  EventLoopThread loop_;
  std::unique_ptr<IExecutionEngine> execution_engine_;
  bool running_{false};
};

}  // namespace quant
