#pragma once

namespace quant {
namespace domain {

// -----------------------------------------------------------------------------
// RiskLimits — engine-wide hard risk thresholds
// -----------------------------------------------------------------------------
//
// @brief  Immutable collection of risk parameters that govern pre-trade and
//         post-trade checks across the engine.
//
// @details
// These limits are applied by PositionEngine (post-trade drawdown monitoring)
// and RiskEngine (pre-trade position sizing). They are passed by value to
// component constructors during engine startup and remain constant for the
// lifetime of the engine.
//
// Sign convention:
//   max_drawdown is a NEGATIVE number representing the realized PnL floor.
//   When a symbol's realized_pnl drops below this threshold, a
//   RiskViolationEvent is published and the RiskEngine halts all signal
//   processing.
//
// In Phase 6 these values will be loaded from a JSON/YAML configuration
// file. Until then, the defaults provide reasonable guardrails for
// backtesting and simulation.
//
// Thread model:
//   RiskLimits is a plain data struct with value semantics. It is copied
//   into components at construction time — no shared mutable state.
//
// Extensibility:
//   Phase 5 (Multi-Strategy) will add per-strategy risk limits alongside
//   these engine-wide limits. Phase 9 (Portfolio Layer) will add
//   portfolio-level limits (max gross exposure, sector concentration).
// -----------------------------------------------------------------------------
struct RiskLimits {
  /// Maximum absolute net position per symbol (in units/contracts).
  /// If a new order would push abs(net_quantity) above this value,
  /// the signal is rejected pre-trade by RiskEngine.
  double max_position_per_symbol{1000.0};

  /// Minimum realized PnL before the kill switch triggers (negative value).
  /// If any symbol's realized_pnl falls below this floor, PositionEngine
  /// publishes a RiskViolationEvent and RiskEngine halts all trading.
  double max_drawdown{-500.0};
};

}  // namespace domain
}  // namespace quant
