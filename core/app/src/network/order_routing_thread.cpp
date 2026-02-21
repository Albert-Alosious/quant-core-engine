#include "quant/network/order_routing_thread.hpp"
#include "quant/execution/execution_engine.hpp"
#include "quant/execution/mock_execution_engine.hpp"

#include <iostream>
#include <utility>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: store time provider for deferred engine creation
// -----------------------------------------------------------------------------
OrderRoutingThread::OrderRoutingThread(const ITimeProvider* time_provider)
    : time_provider_(time_provider) {}

// -----------------------------------------------------------------------------
// Destructor: RAII stop
// -----------------------------------------------------------------------------
OrderRoutingThread::~OrderRoutingThread() { stop(); }

// -----------------------------------------------------------------------------
// start(): start loop thread and create execution engine
// -----------------------------------------------------------------------------
void OrderRoutingThread::start() {
  if (running_) {
    return;
  }

  loop_.start();

  if (time_provider_ != nullptr) {
    execution_engine_ = std::make_unique<MockExecutionEngine>(
        loop_.eventBus(), *time_provider_);
  } else {
    execution_engine_ =
        std::make_unique<ExecutionEngine>(loop_.eventBus());
  }

  running_ = true;

  std::cout << "[OrderRoutingThread] started ("
            << (time_provider_ ? "MockExecution" : "LiveExecution")
            << ").\n";
}

// -----------------------------------------------------------------------------
// stop(): destroy engine and stop loop
// -----------------------------------------------------------------------------
void OrderRoutingThread::stop() {
  if (!running_) {
    return;
  }

  execution_engine_.reset();
  loop_.stop();

  running_ = false;

  std::cout << "[OrderRoutingThread] stopped.\n";
}

// -----------------------------------------------------------------------------
// push(): enqueue event for this thread
// -----------------------------------------------------------------------------
void OrderRoutingThread::push(Event event) {
  loop_.push(std::move(event));
}

// -----------------------------------------------------------------------------
// eventBus(): access the internal bus for cross-thread bridge subscriptions
// -----------------------------------------------------------------------------
EventBus& OrderRoutingThread::eventBus() {
  return loop_.eventBus();
}

}  // namespace quant
