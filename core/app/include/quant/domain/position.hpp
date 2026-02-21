#pragma once

#include <string>

namespace quant {
namespace domain {

// -----------------------------------------------------------------------------
// Position — per-symbol trading state
// -----------------------------------------------------------------------------
//
// @brief  Tracks the net position, average entry price, and realized PnL for
//         a single instrument.
//
// @details
// This is the core domain object for position tracking. It is a plain data
// struct with value semantics — safe to copy between threads via events.
//
// Sign convention for net_quantity:
//   positive → long  (we own the instrument)
//   negative → short (we owe the instrument)
//   zero     → flat  (no position)
//
// average_price represents the weighted average entry cost of the current
// position. It is updated when the position increases (same direction fill)
// and remains unchanged when the position decreases (closing fill). When
// the position crosses zero (reversal), average_price resets to the fill
// price of the new-direction portion.
//
// realized_pnl accumulates the profit/loss from all closed portions of the
// position, computed as:
//   closed_qty * (fill_price - average_price) for longs
//   closed_qty * (average_price - fill_price) for shorts
//
// Thread model:
//   Position is a value type. The authoritative copy lives inside
//   PositionEngine on the risk_execution_loop thread. Snapshots are
//   distributed via PositionUpdateEvent (pass-by-value) through the
//   EventBus.
//
// Ownership:
//   PositionEngine owns the mutable state. Events carry immutable copies.
// -----------------------------------------------------------------------------
struct Position {
  std::string symbol;          // Instrument identifier (e.g. "AAPL")
  double net_quantity{0.0};    // Signed: +long, -short, 0=flat
  double average_price{0.0};   // Weighted avg entry price of current position
  double realized_pnl{0.0};   // Cumulative realized profit/loss
};

}  // namespace domain
}  // namespace quant
