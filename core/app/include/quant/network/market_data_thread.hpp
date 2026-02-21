#pragma once

#include "quant/events/event.hpp"
#include "quant/gateway/market_data_gateway.hpp"
#include "quant/time/simulation_time_provider.hpp"

#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace quant {

// -----------------------------------------------------------------------------
// MarketDataThread — dedicated I/O thread for market data ingestion
// -----------------------------------------------------------------------------
//
// @brief  Encapsulates a std::thread that runs the MarketDataGateway's ZMQ
//         recv loop, isolating network I/O from the core strategy/risk logic.
//
// @details
// Before Phase 4, main() manually created the MarketDataGateway and called
// run() on the main thread. This meant the main thread was blocked in a ZMQ
// recv loop, and SIGINT handling required a global pointer to the gateway.
//
// MarketDataThread wraps both the gateway and its thread into a single RAII
// component that TradingEngine owns. The gateway's event_sink pushes decoded
// MarketDataEvents into the strategy loop's queue — the same mechanism as
// before, but now managed by the engine instead of main().
//
// Why not use EventLoopThread?
//   MarketDataGateway has its own internal blocking recv loop (ZMQ polling
//   with ZMQ_RCVTIMEO). It does not consume from a ThreadSafeQueue. It needs
//   a raw std::thread that calls gateway_->run(), not the pop-dispatch
//   pattern of EventLoopThread.
//
// Thread model:
//   start() spawns a thread that calls gateway_->run(). stop() signals the
//   gateway to exit (atomic flag) and joins the thread. After stop(), the
//   thread is no longer running.
//
//   Constructed and destroyed on the main thread (via TradingEngine).
//   The internal thread runs MarketDataGateway::run() exclusively.
//
// Ownership:
//   Owned by TradingEngine via std::unique_ptr.
//   Owns the MarketDataGateway via std::unique_ptr.
//   Holds a reference to SimulationTimeProvider (owned externally).
//   Holds a copy of the event_sink callback.
// -----------------------------------------------------------------------------
class MarketDataThread {
 public:
  using EventSink = std::function<void(Event)>;

  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  //
  // @brief  Stores parameters for deferred gateway construction.
  //
  // @param  time_provider  Simulation clock reference (must outlive this
  //                        object).
  // @param  event_sink     Callback invoked for each decoded tick. Typically
  //                        bound to TradingEngine::pushEvent().
  // @param  endpoint       ZMQ endpoint the gateway will connect to.
  //
  // @details
  // The MarketDataGateway is NOT created in the constructor — it is created
  // inside start(). This allows TradingEngine to construct MarketDataThread
  // early but defer the ZMQ socket connection until after the
  // synchronization gate completes.
  //
  // Thread-safety: Safe to construct from main().
  // Side-effects:  None — no sockets opened, no threads spawned.
  // -------------------------------------------------------------------------
  MarketDataThread(SimulationTimeProvider& time_provider,
                   EventSink event_sink,
                   std::string endpoint = "tcp://127.0.0.1:5555");

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  //
  // @brief  RAII: calls stop() if the thread is still running.
  //
  // Thread-safety: Must be called from the owning thread (main).
  // -------------------------------------------------------------------------
  ~MarketDataThread();

  MarketDataThread(const MarketDataThread&) = delete;
  MarketDataThread& operator=(const MarketDataThread&) = delete;
  MarketDataThread(MarketDataThread&&) = delete;
  MarketDataThread& operator=(MarketDataThread&&) = delete;

  // -------------------------------------------------------------------------
  // start()
  // -------------------------------------------------------------------------
  //
  // @brief  Creates the MarketDataGateway and spawns the recv thread.
  //
  // @details
  // 1. Constructs the MarketDataGateway (opens ZMQ SUB socket).
  // 2. Spawns a std::thread that calls gateway_->run() (blocking recv loop).
  //
  // Idempotent: calling start() on an already-running thread is a no-op.
  //
  // Thread-safety: Call from the owning thread only (main).
  // Side-effects:  Opens a ZMQ socket and spawns a thread.
  // -------------------------------------------------------------------------
  void start();

  // -------------------------------------------------------------------------
  // stop()
  // -------------------------------------------------------------------------
  //
  // @brief  Signals the gateway to exit and joins the thread.
  //
  // @details
  // Calls gateway_->stop() (sets atomic flag), then joins the thread. The
  // recv loop will notice the flag within MarketDataGateway::kRecvTimeoutMs
  // (100ms) and exit.
  //
  // Idempotent: safe to call multiple times or if never started.
  //
  // Thread-safety: Call from the owning thread only (main).
  // Side-effects:  Blocks until the thread exits.
  // -------------------------------------------------------------------------
  void stop();

 private:
  SimulationTimeProvider& time_provider_;
  EventSink event_sink_;
  std::string endpoint_;

  std::unique_ptr<MarketDataGateway> gateway_;
  std::thread thread_;
};

}  // namespace quant
