#include "quant/risk/position_engine.hpp"
#include "quant/events/position_update_event.hpp"
#include "quant/events/risk_violation_event.hpp"

#include <cmath>
#include <iostream>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: subscribe to OrderEvent and ExecutionReportEvent
// -----------------------------------------------------------------------------
PositionEngine::PositionEngine(EventBus& bus, const domain::RiskLimits& limits)
    : bus_(bus), limits_(limits) {
  // Subscribe to OrderEvent to cache {order_id → symbol, side} before the
  // corresponding fill arrives. Must be registered before the
  // ExecutionReportEvent subscription so the cache is populated first when
  // both events are published in sequence on the same thread.
  order_sub_id_ = bus_.subscribe<OrderEvent>(
      [this](const OrderEvent& e) { onOrder(e); });

  fill_sub_id_ = bus_.subscribe<ExecutionReportEvent>(
      [this](const ExecutionReportEvent& e) { onFill(e); });
}

// -----------------------------------------------------------------------------
// hydratePosition: inject pre-existing position from exchange reconciliation
// -----------------------------------------------------------------------------
void PositionEngine::hydratePosition(const domain::Position& pos) {
  std::unique_lock lock(positions_mutex_);
  positions_[pos.symbol] = pos;
}

// -----------------------------------------------------------------------------
// position: read-only accessor for pre-trade checks by RiskEngine
// -----------------------------------------------------------------------------
const domain::Position* PositionEngine::position(
    const std::string& symbol) const {
  std::shared_lock lock(positions_mutex_);
  auto it = positions_.find(symbol);
  return (it != positions_.end()) ? &it->second : nullptr;
}

// -----------------------------------------------------------------------------
// getSnapshots: thread-safe copy of all positions for IPC server
// -----------------------------------------------------------------------------
std::vector<domain::Position> PositionEngine::getSnapshots() const {
  std::shared_lock lock(positions_mutex_);
  std::vector<domain::Position> result;
  result.reserve(positions_.size());
  for (const auto& [symbol, pos] : positions_) {
    result.push_back(pos);
  }
  return result;
}

// -----------------------------------------------------------------------------
// Destructor: unsubscribe from both event streams
// -----------------------------------------------------------------------------
PositionEngine::~PositionEngine() {
  bus_.unsubscribe(fill_sub_id_);
  bus_.unsubscribe(order_sub_id_);
}

// -----------------------------------------------------------------------------
// onOrder: cache symbol and side for future fill lookup
// -----------------------------------------------------------------------------
void PositionEngine::onOrder(const OrderEvent& event) {
  const domain::Order& order = event.order;
  order_cache_[order.id] = OrderInfo{order.symbol, order.side};
}

// -----------------------------------------------------------------------------
// onFill: update position state and publish PositionUpdateEvent
// -----------------------------------------------------------------------------
void PositionEngine::onFill(const ExecutionReportEvent& event) {
  // Only process filled orders. Rejected fills do not affect positions.
  if (event.status != ExecutionStatus::Filled) {
    return;
  }

  // Look up the symbol and side from the order cache. If the order_id is
  // not found, something is architecturally wrong — log and skip.
  auto it = order_cache_.find(event.order_id);
  if (it == order_cache_.end()) {
    std::cerr << "[PositionEngine] WARNING: no cached order for order_id="
              << event.order_id << ". Skipping fill.\n";
    return;
  }

  const OrderInfo& info = it->second;

  // Compute signed fill quantity based on order side.
  // Buy  → positive fill (increases long / decreases short)
  // Sell → negative fill (increases short / decreases long)
  double signed_fill_qty = (info.side == domain::Side::Buy)
                               ? event.filled_quantity
                               : -event.filled_quantity;

  // Scope the positions_mutex_ lock to cover only the mutation and the
  // snapshot copy. EventBus::publish() is called outside the lock to avoid
  // holding it during subscriber dispatch.
  PositionUpdateEvent update;
  bool drawdown_breached = false;
  RiskViolationEvent violation;

  {
    std::unique_lock lock(positions_mutex_);

    domain::Position& pos = positions_[info.symbol];
    if (pos.symbol.empty()) {
      pos.symbol = info.symbol;
    }

    applyFill(pos, signed_fill_qty, event.fill_price);

    update.position = pos;
    update.timestamp = event.timestamp;
    update.sequence_id = event.sequence_id;

    if (pos.realized_pnl < limits_.max_drawdown) {
      drawdown_breached = true;
      violation.symbol = info.symbol;
      violation.reason = "Max Drawdown Exceeded";
      violation.current_value = pos.realized_pnl;
      violation.limit_value = limits_.max_drawdown;
      violation.timestamp = event.timestamp;
      violation.sequence_id = event.sequence_id;
    }
  }

  bus_.publish(update);

  if (drawdown_breached) {
    bus_.publish(violation);
  }

  // Remove the order from the cache. In the current model each order
  // produces exactly one fill, so the entry is no longer needed.
  order_cache_.erase(it);
}

// -----------------------------------------------------------------------------
// applyFill: core PnL math (static, no side effects beyond pos mutation)
// -----------------------------------------------------------------------------
void PositionEngine::applyFill(domain::Position& pos,
                               double signed_fill_qty,
                               double fill_price) {
  double current_qty = pos.net_quantity;

  // --- Flat position: this is the first fill for this symbol ----------------
  // Equivalent to Case 1 (increasing from zero).
  if (current_qty == 0.0) {
    pos.net_quantity = signed_fill_qty;
    pos.average_price = fill_price;
    return;
  }

  // Determine whether the fill is in the same direction as the existing
  // position. Same sign → increasing; opposite sign → decreasing or reversal.
  bool same_direction =
      (current_qty > 0.0 && signed_fill_qty > 0.0) ||
      (current_qty < 0.0 && signed_fill_qty < 0.0);

  if (same_direction) {
    // ----- Case 1: Increasing position (same direction) ---------------------
    // Weighted average: new_avg = (qty * avg + fill * price) / (qty + fill)
    // Both current_qty and signed_fill_qty have the same sign, so the sum
    // is never zero.
    double new_total = current_qty + signed_fill_qty;
    pos.average_price =
        (current_qty * pos.average_price +
         signed_fill_qty * fill_price) / new_total;
    pos.net_quantity = new_total;
    return;
  }

  // The fill is in the opposite direction. Two sub-cases:
  double abs_current = std::abs(current_qty);
  double abs_fill = std::abs(signed_fill_qty);

  // direction_sign: +1 if current position is long, -1 if short.
  // Used to compute realized PnL:
  //   Long close:  pnl = closed_qty * (fill_price - avg_price)
  //   Short close: pnl = closed_qty * (avg_price - fill_price)
  // Both collapse to: closed_qty * (fill_price - avg_price) * direction_sign
  double direction_sign = (current_qty > 0.0) ? 1.0 : -1.0;

  if (abs_fill <= abs_current) {
    // ----- Case 2: Decreasing position (partial or full close, no reversal) -
    double closed_qty = abs_fill;
    pos.realized_pnl +=
        closed_qty * (fill_price - pos.average_price) * direction_sign;
    pos.net_quantity = current_qty + signed_fill_qty;
    // average_price unchanged.
    return;
  }

  // ----- Case 3: Crossing zero (reversal) -----------------------------------
  // Part A: close the entire existing position.
  double close_qty = abs_current;
  pos.realized_pnl +=
      close_qty * (fill_price - pos.average_price) * direction_sign;

  // Part B: open a new position in the opposite direction.
  double open_qty = abs_fill - abs_current;
  // signed_fill_qty's sign determines the new direction.
  double new_direction_sign = (signed_fill_qty > 0.0) ? 1.0 : -1.0;
  pos.net_quantity = new_direction_sign * open_qty;
  pos.average_price = fill_price;
}

}  // namespace quant
