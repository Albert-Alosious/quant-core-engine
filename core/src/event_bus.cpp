#include "quant/eventbus/event_bus.hpp"
#include <algorithm>

namespace quant {

EventBus::SubscriptionId EventBus::subscribe(GenericCallback callback) {
  std::lock_guard lock(mutex_);
  SubscriptionId id = next_id_++;
  subscribers_.emplace_back(id, std::move(callback));
  return id;
}

void EventBus::unsubscribe(SubscriptionId id) {
  std::lock_guard lock(mutex_);
  subscribers_.erase(
      std::remove_if(subscribers_.begin(), subscribers_.end(),
                     [id](const SubscriberEntry& e) { return e.first == id; }),
      subscribers_.end());
}

void EventBus::publish(const Event& event) {
  std::vector<SubscriberEntry> copy;
  {
    std::lock_guard lock(mutex_);
    copy = subscribers_;
  }
  for (const auto& [id, callback] : copy) {
    callback(event);
  }
}

}  // namespace quant
