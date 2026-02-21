#pragma once

#include "quant/concurrent/order_id_generator.hpp"
#include "quant/domain/order.hpp"
#include "quant/domain/risk_limits.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/events/event_types.hpp"
#include "quant/events/order_event.hpp"
#include "quant/events/risk_violation_event.hpp"

#include <atomic>

namespace quant {

class PositionEngine;  // forward declaration — header-only dependency

// -----------------------------------------------------------------------------
// RiskEngine
// -----------------------------------------------------------------------------
//
// @brief  Converts strategy signals into executable orders after applying
//         pre-trade risk checks and a post-trade kill switch.
//
// @details
// Listens for SignalEvent on the risk_execution_loop's EventBus, applies
// risk checks, converts passing signals into domain::Order objects, wraps
// them in OrderEvent, and publishes them back to the same bus.
//
// Risk checks:
//
//   1. Kill switch (post-trade): RiskEngine subscribes to RiskViolationEvent.
//      If received, halt_trading_ is set to true and ALL subsequent signals
//      are silently dropped. This is the last line of defense against
//      unbounded losses.
//
//   2. Max position check (pre-trade): Before creating an order, RiskEngine
//      queries PositionEngine for the symbol's current net_quantity. If the
//      new order would push abs(net_quantity) above
//      limits_.max_position_per_symbol, the signal is dropped.
//
// Why in architecture:
//   Enforces architecture.md rule #5: "Risk MUST sit between router and
//   execution." Serves as the boundary between strategy intent (SignalEvent)
//   and actual orders sent toward execution.
//
// Thread model:
//   RiskEngine is constructed on main() but its callbacks (onSignal,
//   onRiskViolation) run on the risk_execution_loop thread, because that
//   loop publishes events via its EventBus. The PositionEngine reference
//   is read-only and lives on the same thread — no mutex needed.
//
// Ownership:
//   RiskEngine does not own the EventBus, OrderIdGenerator, or
//   PositionEngine; it holds references to all three. All outlive
//   RiskEngine (destroyed after it in TradingEngine::stop()).
// -----------------------------------------------------------------------------
class RiskEngine {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  //
  // @brief  Subscribes to SignalEvent and RiskViolationEvent on the provided
  //         EventBus.
  //
  // @param  bus        EventBus belonging to risk_execution_loop.
  // @param  id_gen     Thread-safe ID generator owned by TradingEngine.
  // @param  positions  Read-only reference to PositionEngine for pre-trade
  //                    position queries. Must live on the same thread.
  // @param  limits     Engine-wide risk thresholds (copied by value).
  //
  // @details
  // Two subscriptions are registered:
  //   1. SignalEvent → onSignal(): applies risk checks and creates orders.
  //   2. RiskViolationEvent → onRiskViolation(): activates the kill switch.
  //
  // All references must remain valid for the entire lifetime of this
  // RiskEngine instance.
  //
  // Thread-safety: Safe to construct from main() before events start
  //                flowing. subscribe() itself is thread-safe.
  // Side-effects:  Registers two callbacks on the EventBus.
  // -------------------------------------------------------------------------
  RiskEngine(EventBus& bus, OrderIdGenerator& id_gen,
             const PositionEngine& positions,
             const domain::RiskLimits& limits);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  //
  // @brief  Unsubscribes from both SignalEvent and RiskViolationEvent.
  //
  // @details
  // RAII cleanup; ensures no callbacks run after RiskEngine is destroyed.
  // Thread-safety: Safe to destroy from main() after the loop is stopped.
  // -------------------------------------------------------------------------
  ~RiskEngine();

  RiskEngine(const RiskEngine&) = delete;
  RiskEngine& operator=(const RiskEngine&) = delete;
  RiskEngine(RiskEngine&&) = delete;
  RiskEngine& operator=(RiskEngine&&) = delete;

  // -------------------------------------------------------------------------
  // haltTrading()
  // -------------------------------------------------------------------------
  //
  // @brief  Activates the kill switch from an external thread (e.g., the
  //         IPC server thread via a HALT command).
  //
  // @details
  // Sets halt_trading_ to true. All subsequent signals are dropped.
  // This is the programmatic equivalent of receiving a RiskViolationEvent,
  // but triggered externally by an operator rather than by a drawdown
  // breach.
  //
  // Thread-safety: Safe to call from any thread (atomic store).
  // Side-effects:  All future onSignal() calls will drop signals.
  // -------------------------------------------------------------------------
  void haltTrading();

  // -------------------------------------------------------------------------
  // isHalted()
  // -------------------------------------------------------------------------
  //
  // @brief  Returns whether the kill switch is currently active.
  //
  // @return true if trading is halted, false otherwise.
  //
  // @details
  // Used by TradingEngine::executeCommand() to report engine status to
  // the IPC server.
  //
  // Thread-safety: Safe to call from any thread (atomic load).
  // Side-effects:  None (read-only).
  // -------------------------------------------------------------------------
  bool isHalted() const;

 private:
  // -------------------------------------------------------------------------
  // onSignal(event)
  // -------------------------------------------------------------------------
  //
  // @brief  Applies risk checks, then converts a passing SignalEvent into a
  //         domain::Order with a new OrderId and publishes it.
  //
  // @param  event  The incoming signal from a strategy.
  //
  // @details
  // Pre-trade checks (in order):
  //   1. If halt_trading_ is true, drop the signal immediately.
  //   2. Query PositionEngine for the symbol's current net_quantity. If
  //      abs(current + order_qty) > max_position_per_symbol, drop.
  //
  // If all checks pass, build the domain::Order and publish an OrderEvent.
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: May publish an OrderEvent to the EventBus.
  // -------------------------------------------------------------------------
  void onSignal(const SignalEvent& event);

  // -------------------------------------------------------------------------
  // onRiskViolation(event)
  // -------------------------------------------------------------------------
  //
  // @brief  Activates the kill switch, halting all further order creation.
  //
  // @param  event  The RiskViolationEvent from PositionEngine.
  //
  // @details
  // Sets halt_trading_ to true and logs a critical error. Once activated,
  // the kill switch cannot be reset without restarting the engine.
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: Sets halt_trading_ = true.
  // -------------------------------------------------------------------------
  void onRiskViolation(const RiskViolationEvent& event);

  EventBus& bus_;
  OrderIdGenerator& id_gen_;
  const PositionEngine& positions_;
  const domain::RiskLimits limits_;
  std::atomic<bool> halt_trading_{false};
  EventBus::SubscriptionId signal_sub_id_{0};
  EventBus::SubscriptionId violation_sub_id_{0};
};

}  // namespace quant

