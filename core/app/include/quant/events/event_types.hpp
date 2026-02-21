#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace quant {

// -----------------------------------------------------------------------------
// Timestamp
// -----------------------------------------------------------------------------
// Type alias for wall-clock time. Used by all events for ordering and auditing.
// std::chrono::system_clock::time_point is preferred over raw time_t because
// it is type-safe and has well-defined resolution (typically nanoseconds).
// -----------------------------------------------------------------------------
using Timestamp = std::chrono::system_clock::time_point;

// -----------------------------------------------------------------------------
// MarketDataEvent
// -----------------------------------------------------------------------------
// Responsibility: Carries a single market data update (tick) from the market
// data feed into the engine.
// Why in architecture: The market data thread publishes these; the strategy
// thread subscribes to them to generate signals. Keeps market data as an
// immutable snapshot (no global mutable state).
// -----------------------------------------------------------------------------
struct MarketDataEvent {
  std::string symbol;       // Instrument identifier (e.g. "AAPL", "ES")
  double price{0.0};         // Last or mid price for this tick
  double quantity{0.0};     // Volume or size associated with the update
  Timestamp timestamp{};    // When this tick was observed (for ordering)
  std::uint64_t sequence_id{0};  // Monotonic id for total ordering if needed
};

// -----------------------------------------------------------------------------
// SignalEvent
// -----------------------------------------------------------------------------
// Responsibility: Carries a trading signal produced by a strategy (e.g. "buy
// AAPL with strength 0.8 at price 150.25").
// Why in architecture: Strategy thread publishes these; the router/risk layer
// subscribes. Strategy never calls execution directly—it only emits events.
//
// The `price` field carries the market price that triggered the signal. This
// propagates through to the domain::Order and ultimately to the
// ExecutionReportEvent fill price, enabling the PositionEngine to compute
// correct average_price and realized_pnl.
// -----------------------------------------------------------------------------
struct SignalEvent {
  std::string strategy_id;  // Which strategy produced this signal
  std::string symbol;        // Instrument to trade
  enum class Side { Buy, Sell } side{Side::Buy};  // Scoped enum: type-safe
  double strength{0.0};       // Signal strength or size hint (strategy-defined)
  double price{0.0};          // Market price that triggered this signal
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// RiskRejectEvent
// -----------------------------------------------------------------------------
// Responsibility: Informs that an order was rejected by the risk layer (e.g.
// position limit, exposure limit).
// Why in architecture: Risk sits between router and execution; rejections are
// communicated back via events so strategy/monitoring can react without
// direct coupling.
// -----------------------------------------------------------------------------
struct RiskRejectEvent {
  std::string order_id;     // Which order was rejected
  std::string reason;       // Human- or machine-readable rejection reason
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

// -----------------------------------------------------------------------------
// FillEvent
// -----------------------------------------------------------------------------
// Responsibility: Confirms that an order (or part of it) was filled by the
// execution layer.
// Why in architecture: Execution engine publishes these; position manager and
// strategy subscribe. Enables event-driven position and PnL updates.
// -----------------------------------------------------------------------------
struct FillEvent {
  std::string order_id;
  std::string symbol;
  double fill_price{0.0};    // Actual execution price
  double fill_quantity{0.0}; // Filled size (may be partial)
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

// -----------------------------------------------------------------------------
// HeartbeatEvent
// -----------------------------------------------------------------------------
// Responsibility: Periodic status/health message from a component (e.g. market
// data thread, strategy, risk).
// Why in architecture: Supports monitoring and future Python/TCP monitoring;
// allows detecting stalled or disconnected components.
// -----------------------------------------------------------------------------
struct HeartbeatEvent {
  std::string component_id;  // Which component sent this (e.g. "market_data")
  std::string status;        // Optional status string (e.g. "ok", "degraded")
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

}  // namespace quant
