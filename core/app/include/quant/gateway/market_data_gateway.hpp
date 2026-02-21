#pragma once

#include "quant/events/event.hpp"
#include "quant/time/simulation_time_provider.hpp"

#include <zmq.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace quant {

// -----------------------------------------------------------------------------
// MarketDataGateway — ZeroMQ bridge for receiving historical market data
// -----------------------------------------------------------------------------
//
// @brief  Listens on a ZeroMQ SUB socket for JSON-encoded market data ticks,
//         advances the simulation clock, and pushes MarketDataEvent into the
//         engine's event pipeline.
//
// @details
// This is the "Python bridge" for backtesting. A Python script reads
// historical CSV/Parquet data, serializes each tick as JSON, and publishes
// it over ZeroMQ PUB on tcp://127.0.0.1:5555. The MarketDataGateway runs a
// recv loop on a dedicated thread, decodes each message, and injects it into
// the C++ engine as a MarketDataEvent.
//
// On each message the gateway performs two actions IN ORDER:
//   1. advance_time(timestamp_ms) — updates the simulation clock FIRST so
//      that any component reading now_ms() during this tick's processing
//      sees the correct time.
//   2. event_sink_(MarketDataEvent) — pushes the event into the strategy
//      loop's queue for processing on the strategy thread.
//
// Expected JSON format from the Python publisher:
//   {
//     "timestamp_ms": 1700000000000,   // int64 epoch milliseconds
//     "symbol":       "AAPL",          // instrument identifier
//     "price":        150.25,          // last/mid price
//     "volume":       100.0            // tick volume/quantity
//   }
//
// Thread model:
//   run() blocks the calling thread. It is intended to be called from a
//   dedicated std::thread (the "market data thread" in architecture.md).
//   stop() can be called from any thread to request shutdown; it sets an
//   atomic flag that the recv loop checks after every receive timeout.
//
// Shutdown safety (ZMQ_RCVTIMEO):
//   The SUB socket is configured with a receive timeout (ZMQ_RCVTIMEO) so
//   that zmq::socket_t::recv() returns periodically even when no messages
//   arrive. Without this, recv() would block indefinitely and the stop()
//   flag would never be checked, hanging the gateway thread forever during
//   engine shutdown.
//
// Event sink design:
//   The gateway does NOT hold a reference to EventBus or EventLoopThread.
//   Instead it accepts a std::function<void(Event)> callback ("event sink").
//   TradingEngine binds this to strategy_loop_.push(), which enqueues the
//   event for the strategy thread. This keeps the gateway decoupled from
//   the event loop implementation.
//
//   The event_sink signature uses the exact Event (std::variant) type to
//   prevent object slicing. Passing by value is acceptable because Event
//   is a small variant with value semantics, and the sink will std::move
//   it into the queue.
//
// Ownership:
//   - Owns the zmq::context_t and zmq::socket_t (RAII; destroyed in dtor).
//   - Holds a reference to SimulationTimeProvider (owned by TradingEngine).
//   - Holds a copy of the event_sink callback.
// -----------------------------------------------------------------------------
class MarketDataGateway {
 public:
  // Type alias for the event sink callback.
  using EventSink = std::function<void(Event)>;

  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // @brief  Creates the ZMQ context and SUB socket, connects to the
  //         publisher endpoint, and stores the event sink callback.
  //
  // @param  time_provider  SimulationTimeProvider (concrete type — we need
  //                        advance_time(), which is not on ITimeProvider).
  // @param  event_sink     Callback invoked for each decoded MarketDataEvent.
  //                        Must accept Event by value. Typically bound to
  //                        EventLoopThread::push().
  // @param  endpoint       ZMQ endpoint to connect to. Defaults to
  //                        "tcp://127.0.0.1:5555".
  //
  // @details
  // The socket subscribes to all messages (empty filter prefix). ZMQ_RCVTIMEO
  // is set to kRecvTimeoutMs so that recv() returns periodically even without
  // incoming data, allowing the stop flag to be checked.
  //
  // Thread-safety: Safe to construct from the main thread. The socket is not
  //                used until run() is called.
  // Side-effects:  Opens a ZMQ SUB socket and connects to the endpoint.
  // -------------------------------------------------------------------------
  explicit MarketDataGateway(
      SimulationTimeProvider& time_provider,
      EventSink event_sink,
      const std::string& endpoint = "tcp://127.0.0.1:5555");

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // @brief  Closes the ZMQ socket and context (RAII).
  //
  // @details
  // zmq::socket_t and zmq::context_t have RAII destructors that close the
  // underlying resources. No explicit cleanup is needed.
  //
  // Thread-safety: Must not be destroyed while run() is executing on another
  //                thread. Call stop() and join the thread first.
  // -------------------------------------------------------------------------
  ~MarketDataGateway() = default;

  MarketDataGateway(const MarketDataGateway&) = delete;
  MarketDataGateway& operator=(const MarketDataGateway&) = delete;
  MarketDataGateway(MarketDataGateway&&) = delete;
  MarketDataGateway& operator=(MarketDataGateway&&) = delete;

  // -------------------------------------------------------------------------
  // run()
  // -------------------------------------------------------------------------
  // @brief  Blocking recv loop. Call from a dedicated thread.
  //
  // @details
  // Loop:
  //   1. recv(msg, ZMQ_DONTWAIT-style via timeout) — returns after at most
  //      kRecvTimeoutMs if no message arrives.
  //   2. If recv returned a message:
  //      a. Parse JSON to extract timestamp_ms, symbol, price, volume.
  //      b. Advance the simulation clock: time_provider_.advance_time(ts).
  //      c. Construct MarketDataEvent and call event_sink_(event).
  //   3. Check running_ flag. If false, exit loop.
  //
  // Thread-safety: Must be called from exactly one thread. Not reentrant.
  // Side-effects:  Calls advance_time() and event_sink_ on every tick.
  // -------------------------------------------------------------------------
  void run();

  // -------------------------------------------------------------------------
  // stop()
  // -------------------------------------------------------------------------
  // @brief  Requests the recv loop to exit.
  //
  // @details
  // Sets the atomic running_ flag to false. The recv loop will notice this
  // within kRecvTimeoutMs and exit cleanly. The caller should join the
  // gateway thread after calling stop().
  //
  // Thread-safety: Safe to call from any thread.
  // Side-effects:  The run() loop will exit on the next timeout cycle.
  // -------------------------------------------------------------------------
  void stop();

 private:
  // Receive timeout in milliseconds. Controls how often the recv loop
  // checks the stop flag when no messages are arriving. 100ms is responsive
  // enough for shutdown while avoiding busy-wait.
  static constexpr int kRecvTimeoutMs = 100;

  SimulationTimeProvider& time_provider_;
  EventSink event_sink_;

  // ZMQ context and socket. Context is created first and destroyed last
  // (reverse member order). Socket connects to the publisher endpoint.
  zmq::context_t context_{1};
  zmq::socket_t socket_{context_, zmq::socket_type::sub};

  // Atomic stop flag. Set by stop(), read by the recv loop in run().
  std::atomic<bool> running_{false};
};

}  // namespace quant
