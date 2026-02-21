#include "quant/gateway/market_data_gateway.hpp"
#include "quant/events/event_types.hpp"
#include "quant/time/time_utils.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: create ZMQ SUB socket with receive timeout
// -----------------------------------------------------------------------------
MarketDataGateway::MarketDataGateway(SimulationTimeProvider& time_provider,
                                     EventSink event_sink,
                                     const std::string& endpoint)
    : time_provider_(time_provider), event_sink_(std::move(event_sink)) {
  // Subscribe to ALL messages. An empty string filter means "accept
  // everything the publisher sends." If we wanted to filter by topic
  // (e.g. "AAPL" only), we would pass that prefix here.
  socket_.set(zmq::sockopt::subscribe, "");

  // ZMQ_RCVTIMEO: maximum time (milliseconds) that recv() will block
  // before returning with no message. Without this, recv() blocks
  // indefinitely and the stop() flag is never checked, causing the
  // gateway thread to hang during shutdown.
  socket_.set(zmq::sockopt::rcvtimeo, kRecvTimeoutMs);

  // Connect to the publisher endpoint. In backtesting, a Python script
  // runs a ZMQ PUB socket on this address. connect() is non-blocking;
  // the actual TCP handshake happens asynchronously.
  socket_.connect(endpoint);
}

// -----------------------------------------------------------------------------
// run(): blocking recv loop — call from a dedicated thread
// -----------------------------------------------------------------------------
void MarketDataGateway::run() {
  running_.store(true);

  while (running_.load()) {
    zmq::message_t msg;

    // recv() with the previously set ZMQ_RCVTIMEO. Returns a
    // zmq::recv_result_t (std::optional<size_t>). If the timeout expires
    // without a message, the result has no value and we loop back to
    // re-check running_.
    auto result = socket_.recv(msg, zmq::recv_flags::none);

    if (!result.has_value()) {
      // Timeout — no message arrived within kRecvTimeoutMs. Loop back and
      // check the stop flag. This is the mechanism that prevents the thread
      // from hanging during shutdown.
      continue;
    }

    // Convert the raw ZMQ message bytes to a string for JSON parsing.
    // msg.to_string() copies the data; acceptable for tick-rate data.
    std::string payload = msg.to_string();

    try {
      // Parse the JSON payload. nlohmann::json::parse() throws
      // nlohmann::json::parse_error on malformed input.
      auto json = nlohmann::json::parse(payload);

      // Extract fields. at() throws nlohmann::json::out_of_range if a
      // key is missing, which the catch block below handles.
      std::int64_t timestamp_ms = json.at("timestamp_ms").get<std::int64_t>();
      std::string symbol = json.at("symbol").get<std::string>();
      double price = json.at("price").get<double>();
      double volume = json.at("volume").get<double>();

      // --- Step 1: Advance the simulation clock BEFORE publishing --------
      // This ensures that any component calling time_provider_.now_ms()
      // during processing of this tick sees the correct simulated time.
      time_provider_.advance_time(timestamp_ms);

      // --- Step 2: Build and push the MarketDataEvent --------------------
      MarketDataEvent md;
      md.symbol = std::move(symbol);
      md.price = price;
      md.quantity = volume;
      md.timestamp = ms_to_timestamp(timestamp_ms);

      // Push into the strategy loop's queue via the event_sink callback.
      // The callback is std::function<void(Event)>; passing MarketDataEvent
      // implicitly constructs the Event variant (no slicing — Event is
      // std::variant and MarketDataEvent is one of its alternatives).
      event_sink_(std::move(md));

    } catch (const nlohmann::json::exception& e) {
      // Log and skip malformed messages. In production this would go to
      // a structured logger; for Phase 2 stderr is sufficient.
      std::cerr << "[MarketDataGateway] JSON parse error: " << e.what()
                << " — payload: " << payload << "\n";
    }
  }
}

// -----------------------------------------------------------------------------
// stop(): signal the recv loop to exit
// -----------------------------------------------------------------------------
void MarketDataGateway::stop() {
  // Atomic store. The recv loop will see this within kRecvTimeoutMs and
  // exit. The caller should join the gateway thread after calling stop().
  running_.store(false);
}

}  // namespace quant
