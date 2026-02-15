#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace quant {

using Timestamp = std::chrono::system_clock::time_point;

struct MarketDataEvent {
  std::string symbol;
  double price{0.0};
  double quantity{0.0};
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

struct SignalEvent {
  std::string strategy_id;
  std::string symbol;
  enum class Side { Buy, Sell } side{Side::Buy};
  double strength{0.0};
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

struct OrderEvent {
  std::string order_id;
  std::string symbol;
  enum class Side { Buy, Sell } side{Side::Buy};
  double quantity{0.0};
  enum class OrderType { Market, Limit } order_type{OrderType::Market};
  double limit_price{0.0};
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

struct RiskRejectEvent {
  std::string order_id;
  std::string reason;
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

struct FillEvent {
  std::string order_id;
  std::string symbol;
  double fill_price{0.0};
  double fill_quantity{0.0};
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

struct HeartbeatEvent {
  std::string component_id;
  std::string status;
  Timestamp timestamp{};
  std::uint64_t sequence_id{0};
};

}  // namespace quant
