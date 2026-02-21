#include "quant/risk/risk_engine.hpp"
#include "quant/risk/position_engine.hpp"

#include <cmath>
#include <iostream>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: subscribe to SignalEvent and RiskViolationEvent
// -----------------------------------------------------------------------------
RiskEngine::RiskEngine(EventBus& bus, OrderIdGenerator& id_gen,
                       const PositionEngine& positions,
                       const domain::RiskLimits& limits)
    : bus_(bus), id_gen_(id_gen), positions_(positions), limits_(limits) {
  signal_sub_id_ = bus_.subscribe<SignalEvent>(
      [this](const SignalEvent& e) { onSignal(e); });

  violation_sub_id_ = bus_.subscribe<RiskViolationEvent>(
      [this](const RiskViolationEvent& e) { onRiskViolation(e); });
}

// -----------------------------------------------------------------------------
// Destructor: unsubscribe from both streams
// -----------------------------------------------------------------------------
RiskEngine::~RiskEngine() {
  bus_.unsubscribe(violation_sub_id_);
  bus_.unsubscribe(signal_sub_id_);
}

// -----------------------------------------------------------------------------
// onSignal: apply risk checks, then build Order and publish OrderEvent
// -----------------------------------------------------------------------------
void RiskEngine::onSignal(const SignalEvent& event) {
  // --- Kill switch: drawdown violation received, halt all trading -----------
  if (halt_trading_) {
    std::cerr << "[RiskEngine] HALTED — dropping signal for "
              << event.symbol << "\n";
    return;
  }

  // --- Pre-trade position check: would this order exceed the limit? ---------
  const domain::Position* pos = positions_.position(event.symbol);
  double current_qty = pos ? std::abs(pos->net_quantity) : 0.0;
  double proposed_qty = current_qty + 1.0;
  if (proposed_qty > limits_.max_position_per_symbol) {
    std::cerr << "[RiskEngine] Max position limit ("
              << limits_.max_position_per_symbol
              << ") would be exceeded for " << event.symbol
              << " (current=" << current_qty << "). Dropping signal.\n";
    return;
  }

  // --- All checks passed: build and publish the order -----------------------
  domain::OrderId id = id_gen_.next_id();

  domain::Order order;
  order.id = id;
  order.strategy_id = event.strategy_id;
  order.symbol = event.symbol;
  order.side = (event.side == SignalEvent::Side::Buy)
                   ? domain::Side::Buy
                   : domain::Side::Sell;
  order.quantity = 1.0;
  order.price = event.price;

  OrderEvent order_event;
  order_event.order = order;
  order_event.timestamp = std::chrono::system_clock::now();
  order_event.sequence_id = event.sequence_id;

  bus_.publish(order_event);
}

// -----------------------------------------------------------------------------
// onRiskViolation: activate the kill switch
// -----------------------------------------------------------------------------
void RiskEngine::onRiskViolation(const RiskViolationEvent& event) {
  halt_trading_ = true;
  std::cerr << "[RiskEngine] CRITICAL: " << event.reason
            << " for " << event.symbol
            << " (value=" << event.current_value
            << ", limit=" << event.limit_value
            << "). ALL TRADING HALTED.\n";
}

}  // namespace quant

