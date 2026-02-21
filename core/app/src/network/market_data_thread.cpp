#include "quant/network/market_data_thread.hpp"

#include <iostream>
#include <utility>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: store parameters for deferred gateway construction
// -----------------------------------------------------------------------------
MarketDataThread::MarketDataThread(SimulationTimeProvider& time_provider,
                                   EventSink event_sink,
                                   std::string endpoint)
    : time_provider_(time_provider),
      event_sink_(std::move(event_sink)),
      endpoint_(std::move(endpoint)) {}

// -----------------------------------------------------------------------------
// Destructor: RAII stop
// -----------------------------------------------------------------------------
MarketDataThread::~MarketDataThread() { stop(); }

// -----------------------------------------------------------------------------
// start(): create gateway and spawn recv thread
// -----------------------------------------------------------------------------
void MarketDataThread::start() {
  if (thread_.joinable()) {
    return;
  }

  gateway_ = std::make_unique<MarketDataGateway>(time_provider_, event_sink_,
                                                  endpoint_);

  thread_ = std::thread([this] {
    std::cout << "[MarketDataThread] listening on " << endpoint_ << "\n";
    gateway_->run();
    std::cout << "[MarketDataThread] recv loop exited.\n";
  });
}

// -----------------------------------------------------------------------------
// stop(): signal gateway and join thread
// -----------------------------------------------------------------------------
void MarketDataThread::stop() {
  if (gateway_) {
    gateway_->stop();
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  gateway_.reset();
}

}  // namespace quant
