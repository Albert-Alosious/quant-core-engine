#pragma once

#include "quant/domain/order.hpp"
#include "quant/domain/order_status.hpp"
#include "quant/eventbus/event_bus.hpp"
#include "quant/events/execution_report_event.hpp"
#include "quant/events/order_event.hpp"

#include <unordered_map>

namespace quant {

// -----------------------------------------------------------------------------
// OrderTracker — order lifecycle state machine and active order book
// -----------------------------------------------------------------------------
//
// @brief  Tracks every order from creation to terminal state, enforcing
//         valid state transitions and publishing OrderUpdateEvent on each
//         transition.
//
// @details
// The OrderTracker maintains an internal map of all active (non-terminal)
// orders keyed by OrderId. It subscribes to:
//
//   1. OrderEvent — registers a new order with status New.
//   2. ExecutionReportEvent — advances the order's status based on the
//      execution layer's report (Accepted, Filled, Rejected).
//
// State transition validation:
//   Every status change passes through transitionStatus(), which checks
//   the transition against the legal state machine graph. Illegal
//   transitions are logged and rejected — the order remains in its
//   current state.
//
// Terminal states (Filled, Canceled, Rejected, Expired) cause the order
// to be removed from the active map, freeing memory.
//
// Thread model:
//   Lives entirely on the risk_execution_loop thread. All callbacks
//   (onOrder, onExecutionReport) run on that thread. The internal map
//   (active_orders_) is accessed single-threaded — no mutex needed.
//
// Subscriber ordering constraint:
//   OrderTracker must be created BEFORE PositionEngine and ExecutionEngine
//   in TradingEngine::start() so its OrderEvent callback fires first.
//   This ensures the order is registered as New before other components
//   process the same OrderEvent.
//
// Ownership:
//   Owned by TradingEngine via std::unique_ptr. Holds a reference to the
//   EventBus owned by risk_execution_loop's EventLoopThread.
// -----------------------------------------------------------------------------
class OrderTracker {
 public:
  // -------------------------------------------------------------------------
  // Constructor
  // -------------------------------------------------------------------------
  // @brief  Subscribes to OrderEvent and ExecutionReportEvent on the given
  //         EventBus.
  //
  // @param  bus  EventBus belonging to risk_execution_loop.
  //
  // @details
  // Two subscriptions are registered:
  //   1. OrderEvent → onOrder(): registers new order in active_orders_.
  //   2. ExecutionReportEvent → onExecutionReport(): advances order status.
  //
  // Thread-safety: Safe to construct from main() before events flow.
  //                subscribe() itself is thread-safe.
  // Side-effects:  Registers two callbacks on the EventBus.
  // -------------------------------------------------------------------------
  explicit OrderTracker(EventBus& bus);

  // -------------------------------------------------------------------------
  // Destructor
  // -------------------------------------------------------------------------
  // @brief  Unsubscribes from both OrderEvent and ExecutionReportEvent.
  //
  // @details
  // RAII cleanup. After destruction, no callbacks will fire into this
  // object. Must be destroyed before the EventBus is destroyed.
  //
  // Thread-safety: Safe to destroy from main() after the loop is stopped.
  // -------------------------------------------------------------------------
  ~OrderTracker();

  OrderTracker(const OrderTracker&) = delete;
  OrderTracker& operator=(const OrderTracker&) = delete;
  OrderTracker(OrderTracker&&) = delete;
  OrderTracker& operator=(OrderTracker&&) = delete;

  // -------------------------------------------------------------------------
  // hydrateOrder(order)
  // -------------------------------------------------------------------------
  //
  // @brief  Injects a pre-existing open order into the active orders map.
  //
  // @param  order  An Order obtained from the exchange during reconciliation.
  //                The order's status field must reflect the exchange's
  //                current state (e.g., Accepted, PartiallyFilled). The
  //                OrderTracker does NOT validate this status — the exchange
  //                is the source of truth.
  //
  // @details
  // **Warm-up only.** This method must be called from the main thread during
  // the TradingEngine::start() synchronization gate — BEFORE event loop
  // threads are spawned and before any ExecutionReportEvent is processed. It
  // is NOT safe to call concurrently with onOrder() or onExecutionReport().
  //
  // The method does NOT publish an OrderUpdateEvent. Hydrated orders are
  // existing exchange state, not new lifecycle transitions.
  //
  // Thread model: Main thread only, before event loops start.
  // Side-effects: Inserts or overwrites active_orders_[order.id].
  // -------------------------------------------------------------------------
  void hydrateOrder(const domain::Order& order);

  // -------------------------------------------------------------------------
  // transitionStatus(current, next)
  // -------------------------------------------------------------------------
  // @brief  Validates whether a state transition is legal according to the
  //         order lifecycle state machine.
  //
  // @param  current  The order's current status.
  // @param  next     The proposed new status.
  //
  // @return true if the transition is permitted, false otherwise.
  //
  // @details
  // Legal transitions:
  //   New            → PendingNew, Accepted, Rejected
  //   PendingNew     → Accepted, Rejected
  //   Accepted       → PartiallyFilled, Filled, Canceled, Rejected
  //   PartiallyFilled → PartiallyFilled, Filled, Canceled
  //   Filled         → (none — terminal)
  //   Canceled       → (none — terminal)
  //   Rejected       → (none — terminal)
  //   Expired        → (none — terminal)
  //
  // Thread model: Pure function, no side effects, safe to call from any
  //               context.
  // -------------------------------------------------------------------------
  static bool transitionStatus(domain::OrderStatus current,
                               domain::OrderStatus next);

 private:
  // -------------------------------------------------------------------------
  // onOrder(event)
  // -------------------------------------------------------------------------
  // @brief  Registers a new order in the active order map with status New
  //         and publishes an OrderUpdateEvent for the initial state.
  //
  // @param  event  The OrderEvent from RiskEngine.
  //
  // @details
  // The order is copied from the event into active_orders_. Its status is
  // set to New (default) and filled_quantity to 0.0 (default). An
  // OrderUpdateEvent is published with previous_status = New (initial).
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: Inserts into active_orders_, publishes OrderUpdateEvent.
  // -------------------------------------------------------------------------
  void onOrder(const OrderEvent& event);

  // -------------------------------------------------------------------------
  // onExecutionReport(event)
  // -------------------------------------------------------------------------
  // @brief  Advances the order's lifecycle state based on an execution
  //         report and publishes an OrderUpdateEvent.
  //
  // @param  event  The ExecutionReportEvent from the execution layer.
  //
  // @details
  // Steps:
  //   1. Look up order_id in active_orders_. If not found, log and skip.
  //   2. Map ExecutionStatus → OrderStatus:
  //      - Accepted → OrderStatus::Accepted
  //      - Filled   → OrderStatus::Filled (also sets filled_quantity)
  //      - Rejected → OrderStatus::Rejected
  //   3. Validate the transition via transitionStatus(). If illegal, log
  //      and skip.
  //   4. Apply the transition: update status (and filled_quantity for fills).
  //   5. Publish OrderUpdateEvent with the snapshot and previous_status.
  //   6. If the new status is terminal, erase from active_orders_.
  //
  // Thread model: Runs only on the risk_execution_loop thread.
  // Side-effects: Modifies active_orders_, publishes OrderUpdateEvent,
  //               may erase from active_orders_.
  // -------------------------------------------------------------------------
  void onExecutionReport(const ExecutionReportEvent& event);

  // -------------------------------------------------------------------------
  // isTerminal(status)
  // -------------------------------------------------------------------------
  // @brief  Returns true if the given status is a terminal (final) state.
  //
  // @param  status  The OrderStatus to check.
  // @return true for Filled, Canceled, Rejected, Expired.
  // -------------------------------------------------------------------------
  static bool isTerminal(domain::OrderStatus status);

  EventBus& bus_;
  EventBus::SubscriptionId order_sub_id_{0};
  EventBus::SubscriptionId exec_sub_id_{0};

  // Active (non-terminal) orders. Keyed by OrderId. Entries are erased
  // when the order reaches a terminal state.
  std::unordered_map<domain::OrderId, domain::Order> active_orders_;
};

}  // namespace quant
