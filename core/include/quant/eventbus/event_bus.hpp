#pragma once

#include "quant/events/event.hpp"
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace quant {

class EventBus {
 public:
  using GenericCallback = std::function<void(const Event&)>;
  using SubscriptionId = std::size_t;

  EventBus() = default;

  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  SubscriptionId subscribe(GenericCallback callback);

  template <typename EventType>
  SubscriptionId subscribe(std::function<void(const EventType&)> callback);

  void unsubscribe(SubscriptionId id);

  void publish(const Event& event);

 private:
  using SubscriberEntry = std::pair<SubscriptionId, GenericCallback>;

  std::mutex mutex_;
  SubscriptionId next_id_{0};
  std::vector<SubscriberEntry> subscribers_;
};

template <typename EventType>
EventBus::SubscriptionId EventBus::subscribe(
    std::function<void(const EventType&)> callback) {
  GenericCallback wrapped = [cb = std::move(callback)](const Event& event) {
    if (const auto* ptr = std::get_if<EventType>(&event)) {
      cb(*ptr);
    }
  };
  return subscribe(std::move(wrapped));
}

}  // namespace quant
