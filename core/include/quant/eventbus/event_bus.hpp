#pragma once

#include "quant/events/event.hpp"
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace quant {

// -----------------------------------------------------------------------------
// EventBus
// -----------------------------------------------------------------------------
// Responsibility: Central publish-subscribe channel. Subscribers register
// callbacks; publishers post Event values. The bus invokes every matching
// subscriber when an event is published.
//
// Why in architecture: All modules communicate via events (architecture.md).
// The EventBus is the single mechanism—no direct strategy→execution or
// global mutable state. Supports multiple strategies and risk modules
// without coupling.
//
// Thread model: Thread-safe for concurrent subscribe, unsubscribe, and
// publish from any thread. Callbacks run synchronously on the thread that
// calls publish(). (No dedicated dispatcher thread in this design.)
// -----------------------------------------------------------------------------
class EventBus {
 public:
  // Callback type for "all events": receives the Event variant. Subscriber
  // can use std::visit or std::get_if to handle specific types.
  using GenericCallback = std::function<void(const Event&)>;

  // Opaque id returned by subscribe(); pass to unsubscribe() to remove.
  using SubscriptionId = std::size_t;

  EventBus() = default;

  // Non-copyable: the bus owns subscriber state; copying would duplicate
  // or share callbacks and ids, leading to confusion.
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  // -------------------------------------------------------------------------
  // subscribe(GenericCallback)
  // -------------------------------------------------------------------------
  // What: Registers a callback that will be invoked for every published event.
  // Why: Allows a single handler to process all event types (e.g. logger).
  // Thread-safety: Safe to call from any thread; mutex protects subscriber list.
  // Input: callback — invoked as callback(event) for each publish.
  // Output: SubscriptionId to use with unsubscribe().
  // -------------------------------------------------------------------------
  SubscriptionId subscribe(GenericCallback callback);

  // -------------------------------------------------------------------------
  // subscribe<EventType>(callback)
  // -------------------------------------------------------------------------
  // What: Registers a callback that is invoked only when the published event
  // holds a value of type EventType (e.g. MarketDataEvent).
  // Why: Subscribers avoid manual std::get_if/visit; they receive the
  // concrete type. Type-safe and clearer call sites.
  // Thread-safety: Same as subscribe(GenericCallback); implemented by
  // wrapping in a generic callback that checks the variant type.
  // Input: callback — invoked as callback(concrete_event) only for EventType.
  // Output: SubscriptionId to use with unsubscribe().
  // -------------------------------------------------------------------------
  template <typename EventType>
  SubscriptionId subscribe(std::function<void(const EventType&)> callback);

  // -------------------------------------------------------------------------
  // unsubscribe(id)
  // -------------------------------------------------------------------------
  // What: Removes the subscription with the given id. Future publishes will
  // not invoke that callback.
  // Why: Allows components to detach when shutting down or reconfiguring.
  // Thread-safety: Safe to call from any thread. If publish() is in progress,
  // the callback may still run for the current event; it will not run for
  // subsequent events.
  // Input: id — value returned by a previous subscribe() call.
  // Output: None.
  // -------------------------------------------------------------------------
  void unsubscribe(SubscriptionId id);

  // -------------------------------------------------------------------------
  // publish(event)
  // -------------------------------------------------------------------------
  // What: Delivers the event to all currently registered subscribers by
  // invoking their callbacks. Callbacks run on the calling thread, before
  // publish() returns.
  // Why: This is the only way to send events through the engine; keeps
  // communication event-based.
  // Thread-safety: Safe to call from any thread. We copy the subscriber list
  // under the lock, then run callbacks without holding the lock so that a
  // callback that calls publish() or unsubscribe() cannot deadlock.
  // Input: event — copied into the variant; subscribers receive const Event&.
  // Output: None.
  // -------------------------------------------------------------------------
  void publish(const Event& event);

 private:
  // One entry: id (for unsubscribe) and the callback to invoke.
  using SubscriberEntry = std::pair<SubscriptionId, GenericCallback>;

  std::mutex mutex_;              // Protects subscribers_ and next_id_
  SubscriptionId next_id_{0};     // Monotonically increasing id for new subs
  std::vector<SubscriberEntry> subscribers_;
};

// -----------------------------------------------------------------------------
// Template implementation: typed subscribe
// -----------------------------------------------------------------------------
// Wraps the typed callback in a generic callback that uses std::get_if to
// check the variant. If the event holds EventType, the inner callback is
// called; otherwise the event is ignored. Move capture [cb = std::move(...)]
// moves the callback into the lambda so we do not copy it.
// -----------------------------------------------------------------------------
template <typename EventType>
EventBus::SubscriptionId EventBus::subscribe(
    std::function<void(const EventType&)> callback) {
  GenericCallback wrapped = [cb = std::move(callback)](const Event& event) {
    // std::get_if: returns pointer to the value if variant holds EventType,
    // else nullptr. Avoids exceptions and keeps code linear.
    if (const auto* ptr = std::get_if<EventType>(&event)) {
      cb(*ptr);
    }
  };
  return subscribe(std::move(wrapped));
}

}  // namespace quant
