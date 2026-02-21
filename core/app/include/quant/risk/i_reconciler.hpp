#pragma once

#include "quant/domain/order.hpp"
#include "quant/domain/position.hpp"

#include <vector>

namespace quant {

// -----------------------------------------------------------------------------
// IReconciler — exchange state reconciliation interface
// -----------------------------------------------------------------------------
//
// @brief  Defines the contract for querying an exchange (or mock) to discover
//         positions and open orders that exist outside the engine's memory.
//
// @details
// On startup the engine may not know what positions or orders the broker
// currently holds from a previous session, a manual trade, or a crash
// recovery scenario. An IReconciler implementation queries the authoritative
// source (exchange REST API, local journal, etc.) and returns plain domain
// objects that the engine can ingest before it begins processing market data.
//
// Calling convention:
//   Both methods are called exactly once, synchronously, on the **main
//   thread** during the TradingEngine::start() synchronization gate — before
//   event loop threads are spawned. They must return promptly; blocking
//   indefinitely will delay engine startup.
//
// Ownership:
//   TradingEngine does NOT own the reconciler. It receives a non-owning
//   pointer (IReconciler*) in start() and uses it only during the warm-up
//   phase. The caller (main()) owns the reconciler's lifetime.
//
// Thread model:
//   NOT thread-safe. Called from a single thread (main) before any event
//   processing begins. Implementations must not rely on engine components
//   being active.
// -----------------------------------------------------------------------------
class IReconciler {
 public:
  virtual ~IReconciler() = default;

  // -------------------------------------------------------------------------
  // reconcilePositions()
  // -------------------------------------------------------------------------
  //
  // @brief  Returns the exchange's current position state for all instruments.
  //
  // @return A vector of domain::Position structs. Each entry represents one
  //         instrument's net position, average entry price, and realized PnL
  //         as reported by the exchange. An empty vector means no positions
  //         exist.
  //
  // @details
  // The returned positions are injected into PositionEngine via
  // hydratePosition() before any MarketDataEvent is processed. This ensures
  // the PnL math starts from the correct baseline rather than assuming flat.
  //
  // Thread model: Called on main thread only, before event loops start.
  // Side-effects: Implementation-defined (may perform network I/O).
  // -------------------------------------------------------------------------
  virtual std::vector<domain::Position> reconcilePositions() = 0;

  // -------------------------------------------------------------------------
  // reconcileOrders()
  // -------------------------------------------------------------------------
  //
  // @brief  Returns all open (non-terminal) orders currently on the exchange.
  //
  // @return A vector of domain::Order structs. Each entry represents an order
  //         that the exchange considers active (e.g., Accepted or
  //         PartiallyFilled). An empty vector means no open orders exist.
  //
  // @details
  // The returned orders are injected into OrderTracker via hydrateOrder()
  // before any MarketDataEvent is processed. This prevents the OrderTracker
  // from rejecting execution reports for orders it doesn't know about.
  //
  // The caller (TradingEngine) does NOT validate order status — the exchange
  // is the source of truth.
  //
  // Thread model: Called on main thread only, before event loops start.
  // Side-effects: Implementation-defined (may perform network I/O).
  // -------------------------------------------------------------------------
  virtual std::vector<domain::Order> reconcileOrders() = 0;
};

// -----------------------------------------------------------------------------
// MockReconciler — simulation/test reconciler with hardcoded state
// -----------------------------------------------------------------------------
//
// @brief  Returns a single hardcoded position (100 shares AAPL at $150.00)
//         and no open orders. Used for testing the synchronization gate
//         without a real exchange connection.
//
// @details
// This implementation exists so we can verify the warm-up → live transition
// in unit tests and simulation mode. It will be replaced by a
// RestApiReconciler in Phase 4 when the engine connects to a real exchange.
//
// Thread model: Same as IReconciler — main thread only, before event loops.
// Ownership:    Typically stack-allocated in main() or test fixtures.
// -----------------------------------------------------------------------------
class MockReconciler : public IReconciler {
 public:
  // -------------------------------------------------------------------------
  // reconcilePositions()
  // -------------------------------------------------------------------------
  //
  // @brief  Returns one position: 100 shares of AAPL at $150.00.
  //
  // @return A single-element vector with the mock position.
  //
  // @details
  // Simulates the scenario where the engine restarts and the broker still
  // holds a position from the previous session.
  //
  // Thread model: Main thread only.
  // Side-effects: None.
  // -------------------------------------------------------------------------
  std::vector<domain::Position> reconcilePositions() override {
    domain::Position pos;
    pos.symbol = "AAPL";
    pos.net_quantity = 100.0;
    pos.average_price = 150.0;
    pos.realized_pnl = 0.0;
    return {pos};
  }

  // -------------------------------------------------------------------------
  // reconcileOrders()
  // -------------------------------------------------------------------------
  //
  // @brief  Returns an empty vector (no open orders from a previous session).
  //
  // @return An empty vector.
  //
  // @details
  // In this mock scenario all previous orders have reached terminal states.
  // A real reconciler would query the exchange's open-orders endpoint.
  //
  // Thread model: Main thread only.
  // Side-effects: None.
  // -------------------------------------------------------------------------
  std::vector<domain::Order> reconcileOrders() override {
    return {};
  }
};

}  // namespace quant
