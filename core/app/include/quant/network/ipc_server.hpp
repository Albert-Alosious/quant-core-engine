#pragma once

#include "quant/concurrent/thread_safe_queue.hpp"
#include "quant/domain/order_status.hpp"
#include "quant/events/event.hpp"
#include "quant/events/order_update_event.hpp"
#include "quant/events/position_update_event.hpp"
#include "quant/events/risk_violation_event.hpp"

#include <zmq.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace quant {

// -----------------------------------------------------------------------------
// IpcServer — dual-socket ZeroMQ IPC gateway for telemetry and commands
// -----------------------------------------------------------------------------
//
// @brief  Runs a dedicated thread that broadcasts real-time telemetry events
//         to external subscribers (PUB socket) and accepts command requests
//         from external clients (REP socket).
//
// @details
// The IPC server fulfills Phase 4.2 of the roadmap: "ZeroMQ IPC Server
// (Remote control via Python/Telegram bot)."
//
// Two ZeroMQ sockets operate on the same thread:
//
//   1. PUB socket (port 5557, configurable):
//      Broadcasts JSON-formatted telemetry for OrderUpdateEvent,
//      PositionUpdateEvent, and RiskViolationEvent. Events arrive via a
//      ThreadSafeQueue from the risk_loop thread — the queue acts as a
//      buffer so JSON serialization and ZMQ I/O never block the hot path.
//
//   2. REP socket (port 5556, configurable):
//      Accepts command strings from a REQ client. Each received command is
//      forwarded to a callback (bound to TradingEngine::executeCommand()),
//      and the JSON response is sent back. The REP socket uses
//      ZMQ_RCVTIMEO so it does not block indefinitely — the thread
//      alternates between command polling and telemetry draining.
//
// Thread model:
//   Constructed and destroyed on the main thread (via TradingEngine).
//   start() spawns a worker thread that runs the combined poll/drain loop.
//   stop() sets an atomic flag and joins the thread.
//
//   The telemetry queue is written to from the risk_loop thread (via
//   pushTelemetry()) and read from the IPC thread — ThreadSafeQueue
//   handles synchronization.
//
//   The command_handler_ callback is invoked on the IPC thread. It calls
//   TradingEngine::executeCommand(), which in turn calls thread-safe
//   accessors on PositionEngine (shared_mutex) and RiskEngine (atomic).
//
// Ownership:
//   Owned by TradingEngine via std::unique_ptr.
//   Owns the ZMQ context, both sockets, the telemetry queue, and the
//   worker thread.
//   Holds a copy of the command_handler_ callback.
// -----------------------------------------------------------------------------
class IpcServer {
 public:
  using CommandHandler = std::function<std::string(const std::string&)>;

  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  //
  // @brief  Stores parameters for deferred socket creation.
  //
  // @param  command_handler  Callback invoked for each command received on
  //                          the REP socket. Takes a command string, returns
  //                          a JSON response string. Typically bound to
  //                          TradingEngine::executeCommand().
  // @param  cmd_endpoint     ZMQ endpoint for the REP command socket.
  // @param  pub_endpoint     ZMQ endpoint for the PUB telemetry socket.
  //
  // @details
  // No sockets are opened and no threads are spawned in the constructor.
  // Call start() to bring the server online.
  //
  // Thread-safety: Safe to construct from main().
  // Side-effects:  None.
  // -------------------------------------------------------------------------
  explicit IpcServer(CommandHandler command_handler,
                     std::string cmd_endpoint = "tcp://127.0.0.1:5556",
                     std::string pub_endpoint = "tcp://127.0.0.1:5557");

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  //
  // @brief  RAII: calls stop() if the thread is still running.
  // -------------------------------------------------------------------------
  ~IpcServer();

  IpcServer(const IpcServer&) = delete;
  IpcServer& operator=(const IpcServer&) = delete;
  IpcServer(IpcServer&&) = delete;
  IpcServer& operator=(IpcServer&&) = delete;

  // -------------------------------------------------------------------------
  // start()
  // -------------------------------------------------------------------------
  //
  // @brief  Opens the ZMQ sockets and spawns the IPC worker thread.
  //
  // @details
  // 1. Creates the ZMQ context.
  // 2. Binds the REP socket (commands) and PUB socket (telemetry).
  // 3. Sets ZMQ_RCVTIMEO on the REP socket for non-blocking polling.
  // 4. Spawns the worker thread running run().
  //
  // Idempotent: calling start() when already running is a no-op.
  //
  // Thread-safety: Call from the owning thread only (main).
  // Side-effects:  Opens two ZMQ sockets, spawns a thread.
  // -------------------------------------------------------------------------
  void start();

  // -------------------------------------------------------------------------
  // stop()
  // -------------------------------------------------------------------------
  //
  // @brief  Signals the worker to exit and joins the thread.
  //
  // @details
  // Sets running_ to false. The worker thread will notice within
  // kPollTimeoutMs and exit. Closes the ZMQ sockets after join.
  //
  // Idempotent: safe to call multiple times or if never started.
  //
  // Thread-safety: Call from the owning thread only (main).
  // Side-effects:  Blocks until the worker thread exits.
  // -------------------------------------------------------------------------
  void stop();

  // -------------------------------------------------------------------------
  // pushTelemetry(event)
  // -------------------------------------------------------------------------
  //
  // @brief  Enqueues a telemetry event for broadcasting on the PUB socket.
  //
  // @param  event  The Event variant (typically OrderUpdateEvent,
  //                PositionUpdateEvent, or RiskViolationEvent).
  //
  // @details
  // The event is moved into the thread-safe queue. The IPC worker thread
  // drains the queue on each loop iteration, formats JSON, and publishes.
  //
  // This method is designed to be called from EventBus bridge subscribers
  // on the risk_loop thread. The queue ensures zero blocking on the hot
  // path — push is O(1) amortized.
  //
  // Thread-safety: Safe to call from any thread.
  // Side-effects:  Enqueues the event.
  // -------------------------------------------------------------------------
  void pushTelemetry(Event event);

 private:
  static constexpr int kPollTimeoutMs = 50;

  // -------------------------------------------------------------------------
  // run() — worker thread entry point
  // -------------------------------------------------------------------------
  //
  // @brief  Combined poll/drain loop for commands and telemetry.
  //
  // @details
  // Loop (while running_):
  //   1. Drain telemetry queue: try_pop() repeatedly until empty, format
  //      each event as JSON, publish on the PUB socket.
  //   2. Poll REP socket with kPollTimeoutMs timeout. If a command
  //      arrives, invoke command_handler_ and send the response.
  //
  // Thread model: Runs exclusively on the IPC worker thread.
  // -------------------------------------------------------------------------
  void run();

  // -------------------------------------------------------------------------
  // processTelemetry()
  // -------------------------------------------------------------------------
  //
  // @brief  Drains the telemetry queue and publishes events as JSON.
  //
  // @details
  // Non-blocking: calls try_pop() until the queue is empty. For each
  // event, calls formatTelemetry() and publishes the resulting JSON
  // string on the PUB socket.
  //
  // Thread model: Called only from run() on the IPC worker thread.
  // Side-effects: Sends messages on the PUB socket.
  // -------------------------------------------------------------------------
  void processTelemetry();

  // -------------------------------------------------------------------------
  // processCommands()
  // -------------------------------------------------------------------------
  //
  // @brief  Polls the REP socket for a command and responds.
  //
  // @details
  // Calls recv() with ZMQ_RCVTIMEO. If a message arrives, converts it
  // to a string, passes it to command_handler_, and sends the response.
  // If recv times out, returns immediately.
  //
  // Thread model: Called only from run() on the IPC worker thread.
  // Side-effects: May send a response on the REP socket.
  // -------------------------------------------------------------------------
  void processCommands();

  // -------------------------------------------------------------------------
  // formatTelemetry(event)
  // -------------------------------------------------------------------------
  //
  // @brief  Converts a telemetry Event into a JSON string.
  //
  // @param  event  The Event variant to format.
  //
  // @return JSON string if the event is a telemetry type (OrderUpdateEvent,
  //         PositionUpdateEvent, RiskViolationEvent). std::nullopt for
  //         other event types (which should not arrive in the queue but
  //         are handled defensively).
  //
  // Thread model: Called only from processTelemetry() on the IPC thread.
  // Side-effects: None (pure function).
  // -------------------------------------------------------------------------
  static std::optional<std::string> formatTelemetry(const Event& event);

  static std::string formatOrderUpdate(const OrderUpdateEvent& e);
  static std::string formatPositionUpdate(const PositionUpdateEvent& e);
  static std::string formatRiskViolation(const RiskViolationEvent& e);

  // -------------------------------------------------------------------------
  // orderStatusToString / sideToString
  // -------------------------------------------------------------------------
  //
  // @brief  Convert enum values to human-readable strings for JSON.
  // -------------------------------------------------------------------------
  static const char* orderStatusToString(domain::OrderStatus s);
  static const char* sideToString(domain::Side s);

  CommandHandler command_handler_;
  std::string cmd_endpoint_;
  std::string pub_endpoint_;

  std::unique_ptr<zmq::context_t> context_;
  std::unique_ptr<zmq::socket_t> cmd_socket_;
  std::unique_ptr<zmq::socket_t> pub_socket_;

  ThreadSafeQueue<Event> telemetry_queue_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace quant
