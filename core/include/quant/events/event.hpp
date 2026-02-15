#pragma once

#include "event_types.hpp"
#include <variant>

namespace quant {

using Event = std::variant<MarketDataEvent, SignalEvent, OrderEvent,
                          RiskRejectEvent, FillEvent, HeartbeatEvent>;

}  // namespace quant
