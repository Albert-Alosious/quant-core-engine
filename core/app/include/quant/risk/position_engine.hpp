#pragma once

#include "quant/domain/order.hpp"
#include "quant/domain/position.hpp"
#include "quant/domain/risk_limits.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/events/execution_report_event.hpp"
#include "quant/events/order_event.hpp"

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace quant {

// -----------------------------------------------------------------------------
// PositionEngine — per-symbol position tracker and PnL calculator
// -----------------------------------------------------------------------------
//
// @brief  Subscribes to OrderEvent and ExecutionReportEvent on the
//         risk_execution_loop's EventBus. Tracks net position per symbol
//         and computes realized PnL on every fill. Publishes a
//         PositionUpdateEvent after each state change.
//
// @details
// The PositionEngine is the first stateful component in the engine. It
// maintains two internal maps:
//
//   1. positions_: maps symbol → Position (net_quantity, average_price,
//      realized_pnl). Updated on every filled ExecutionReportEvent.
//
//   2. order_cache_: maps order_id → {symbol, side}. Needed because
//      ExecutionReportEvent carries order_id, filled_quantity, and
//      fill_price but NOT symbol or side. The PositionEngine subscribes
//      to OrderEvent to cache this mapping before the fill arrives.
//
// Why is the order cache necessary?
//   ExecutionReportEvent was designed as a minimal fill report (Phase 1).
//   Adding symbol/side to it would change the existing event contract.
//   Instead, the PositionEngine observes the OrderEvent (which contains
//   the full domain::Order with symbol and side) and caches the relevant
//   fields. Since both OrderEvent and ExecutionReportEvent are published
//   on the same risk_execution_loop thread, OrderEvent is guaranteed to
//   arrive before its corresponding ExecutionReportEvent for the same
//   order_id — no race condition.
//
// PnL math rules (strictly enforced):
//
//   Case 1 — Increasing position (fill in same direction as position):
//     new_avg = (current_qty * current_avg + fill_qty * fill_price)
//               / (current_qty + fill_qty)
//     current_qty += fill_qty
//     realized_pnl unchanged.
//
//   Case 2 — Decreasing position (fill in opposite direction, partial close):
//     closed_qty = min(abs(fill_qty), abs(current_qty))
//     realized_pnl += closed_qty * (fill_price - avg_price) * direction_sign
//       where direction_sign = +1 if closing a long, -1 if closing a short
//     current_qty += fill_qty  (fill_qty is signed opposite to current)
//     average_price unchanged.
//
//   Case 3 — Crossing zero (fill exceeds current position, reversal):
//     Split the fill into two parts:
//       a) close_qty closes the existing position to 0 → apply Case 2.
//       b) open_qty = abs(fill_qty) - abs(current_qty) opens a new position
//          in the opposite direction → average_price = fill_price.
//     current_qty = signed open_qty in the new direction.
//
// Thread model:
//   Lives entirely on the risk_execution_loop thread. All callbacks
//   (onOrder, onFill) run on that thread. The internal maps (positions_,
//   order_cache_) are accessed single-threaded — no mutex needed.
//
// Post-trade risk monitoring:
//   After each fill, if the symbol's realized_pnl drops below
//   limits_.max_drawdown, a RiskViolationEvent is published. RiskEngine
//   subscribes to this event and activates its kill switch.
//
// Ownership:
//   Owned by TradingEngine via std::unique_ptr. Holds a reference to the
//   EventBus owned by risk_execution_loop's EventLoopThread.
// -----------------------------------------------------------------------------
class PositionEngine {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // @brief  Subscribes to OrderEvent and ExecutionReportEvent on the given
  //         EventBus, and stores the risk limits for post-trade monitoring.
  //
  // @param  bus     EventBus belonging to risk_execution_loop.
  // @param  limits  Engine-wide risk thresholds (copied by value).
  //
  // @details
  // Two subscriptions are registered:
  //   1. OrderEvent → onOrder(): caches {order_id → symbol, side} so
  //      fills can be attributed to a symbol and direction.
  //   2. ExecutionReportEvent → onFill(): updates positions, publishes
  //      PositionUpdateEvent, and checks drawdown limits.
  //
  // Thread-safety: Safe to construct from main() before events flow.
  //                subscribe() itself is thread-safe.
  // Side-effects:  Registers two callbacks on the EventBus.
  // -------------------------------------------------------------------------
  PositionEngine(EventBus& bus, const domain::RiskLimits& limits);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // @brief  Unsubscribes from both OrderEvent and ExecutionReportEvent.
  //
  // @details
  // RAII cleanup. After destruction, no callbacks will fire into this
  // object. Must be called before the EventBus is destroyed (i.e. before
  // the event loop is stopped).
  //
  // Thread-safety: Safe to destroy from main() after the loop is stopped.
  // -------------------------------------------------------------------------
  ~PositionEngine();

  PositionEngine(const PositionEngine&) = delete;
  PositionEngine& operator=(const PositionEngine&) = delete;
  PositionEngine(PositionEngine&&) = delete;
  PositionEngine& operator=(PositionEngine&&) = delete;

  // -------------------------------------------------------------------------
  // hydratePosition(pos)
  // -------------------------------------------------------------------------
  //
  // @brief  Injects a pre-existing position into the internal positions map.
  //
  // @param  pos  A Position obtained from the exchange or a previous session.
  //              The symbol, net_quantity, average_price, and realized_pnl
  //              fields are all used as-is.
  //
  // @details
  // **Warm-up only.** This method must be called from the main thread during
  // the TradingEngine::start() synchronization gate — BEFORE event loop
  // threads are spawned and before any MarketDataEvent is processed. It is
  // NOT safe to call concurrently with onFill().
  //
  // The method does NOT publish a PositionUpdateEvent. Hydrated positions
  // are historical state from a previous session, not live trading activity.
  //
  // Thread model: Main thread only, before event loops start.
  // Side-effects: Inserts or overwrites positions_[pos.symbol].
  // -------------------------------------------------------------------------
  void hydratePosition(const domain::Position& pos);

  // -------------------------------------------------------------------------
  // position(symbol)
  // -------------------------------------------------------------------------
  //
  // @brief  Returns a read-only pointer to the position for the given symbol,
  //         or nullptr if no position exists.
  //
  // @param  symbol  The instrument identifier to look up.
  //
  // @return Const pointer to the Position if it exists, nullptr otherwise.
  //
  // @details
  // This accessor is used by RiskEngine for pre-trade position checks. Both
  // PositionEngine and RiskEngine live on the risk_execution_loop thread, so
  // this read is single-threaded and safe without a mutex.
  //
  // The returned pointer is valid only until the next call to onFill() (which
  // may insert or modify entries). In practice, RiskEngine::onSignal() uses
  // it within a single callback invocation — the pointer is never stored.
  //
  // Thread model: Safe to call from the risk_execution_loop thread only.
  // Side-effects: None (read-only).
  // -------------------------------------------------------------------------
  const domain::Position* position(const std::string& symbol) const;

  // -------------------------------------------------------------------------
  // getSnapshots()
  // -------------------------------------------------------------------------
  //
  // @brief  Returns a copy of all current positions as a vector.
  //
  // @return A vector of Position structs, one per tracked symbol. The
  //         vector is a deep copy — safe to use from any thread after
  //         the call returns.
  //
  // @details
  // This method is designed for cross-thread access by the IPC server.
  // It acquires a shared_lock on positions_mutex_ so it can run
  // concurrently with other readers (e.g., position()) but waits for
  // any active writer (onFill(), hydratePosition()) to finish.
  //
  // Thread model: Safe to call from any thread.
  // Side-effects: None (read-only).
  // -------------------------------------------------------------------------
  std::vector<domain::Position> getSnapshots() const;

 private:
  // Lightweight struct cached from OrderEvent. We only need symbol and side
  // to process the fill; storing the full domain::Order would be wasteful.
  struct OrderInfo {
    std::string symbol;
    domain::Side side{domain::Side::Buy};
  };

  // -------------------------------------------------------------------------
  // onOrder(event)
  // -------------------------------------------------------------------------
  // @brief  Caches symbol and side from an OrderEvent for future fill
  //         lookup.
  //
  // @param  event  The OrderEvent containing the full domain::Order.
  //
  // @details
  // Extracts order.id, order.symbol, and order.side and stores them in
  // order_cache_. This must happen before the corresponding
  // ExecutionReportEvent arrives — guaranteed by single-threaded
  // dispatch on the risk_execution_loop.
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: Inserts into order_cache_.
  // -------------------------------------------------------------------------
  void onOrder(const OrderEvent& event);

  // -------------------------------------------------------------------------
  // onFill(event)
  // -------------------------------------------------------------------------
  // @brief  Processes a filled ExecutionReportEvent: updates the position
  //         for the relevant symbol and publishes a PositionUpdateEvent.
  //
  // @param  event  The ExecutionReportEvent with Filled status.
  //
  // @details
  // Steps:
  //   1. Look up order_id in order_cache_ to retrieve symbol and side.
  //   2. Compute signed fill quantity (+fill_qty for Buy, -fill_qty for
  //      Sell).
  //   3. Apply the appropriate PnL math (increasing, decreasing, or
  //      crossing zero) to positions_[symbol].
  //   4. Publish a PositionUpdateEvent with a snapshot of the updated
  //      position.
  //   5. Erase the order from order_cache_ (each order produces exactly
  //      one fill in the current model).
  //
  // Rejected fills (status != Filled) are silently ignored.
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: Modifies positions_, erases from order_cache_,
  //               publishes PositionUpdateEvent to the EventBus.
  // -------------------------------------------------------------------------
  void onFill(const ExecutionReportEvent& event);

  // -------------------------------------------------------------------------
  // applyFill(position, signed_fill_qty, fill_price)
  // -------------------------------------------------------------------------
  // @brief  Core PnL math: applies a signed fill to an existing position.
  //
  // @param  pos              Reference to the Position to update in-place.
  // @param  signed_fill_qty  Positive for Buy, negative for Sell.
  // @param  fill_price       Price at which the fill occurred.
  //
  // @details
  // Implements the three strict math cases:
  //
  //   Case 1 — Increasing (same direction):
  //     new_avg = (qty * avg + fill_qty * fill_price) / (qty + fill_qty)
  //     qty += fill_qty
  //
  //   Case 2 — Decreasing (opposite direction, does not cross zero):
  //     closed = abs(fill_qty)
  //     realized_pnl += closed * (fill_price - avg) * sign(qty)
  //     qty += fill_qty
  //     avg unchanged
  //
  //   Case 3 — Crossing zero (reversal):
  //     close_qty = abs(qty)           (close the entire existing position)
  //     realized_pnl += close_qty * (fill_price - avg) * sign(qty)
  //     open_qty = abs(fill_qty) - close_qty
  //     qty = sign(fill_qty) * open_qty
  //     avg = fill_price               (new position starts at fill price)
  //
  // Thread model: Called only from onFill() on the risk_execution_loop.
  // Side-effects: Modifies pos in-place.
  // -------------------------------------------------------------------------
  static void applyFill(domain::Position& pos,
                        double signed_fill_qty,
                        double fill_price);

  EventBus& bus_;
  EventBus::SubscriptionId order_sub_id_{0};
  EventBus::SubscriptionId fill_sub_id_{0};

  const domain::RiskLimits limits_;

  // Protects positions_ for cross-thread reads (IPC server's getSnapshots()
  // and position() from the risk loop). Writers (onFill, hydratePosition)
  // acquire unique_lock; readers acquire shared_lock.
  mutable std::shared_mutex positions_mutex_;

  // Per-symbol position state. Keyed by symbol string.
  std::unordered_map<std::string, domain::Position> positions_;

  // Order cache: maps order_id → {symbol, side}. Populated by onOrder(),
  // consumed and erased by onFill(). Single-threaded access.
  std::unordered_map<domain::OrderId, OrderInfo> order_cache_;
};

}  // namespace quant
