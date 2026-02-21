#include "quant/eventbus/event_bus.hpp"
#include <algorithm>

namespace quant {

// -----------------------------------------------------------------------------
// subscribe(GenericCallback)
// -----------------------------------------------------------------------------
EventBus::SubscriptionId EventBus::subscribe(GenericCallback callback) {
  // std::lock_guard: RAII mutex lock. Lock is acquired here and released when
  // the scope exits (no manual unlock). Prevents forgetting to unlock and
  // avoids deadlocks from early returns or exceptions.
  std::lock_guard lock(mutex_);

  // Assign a unique id so this subscription can be removed later.
  SubscriptionId id = next_id_++;

  // emplace_back: constructs the pair in-place; std::move(callback) avoids
  // copying the potentially heavy std::function.
  subscribers_.emplace_back(id, std::move(callback));

  return id;
}

// -----------------------------------------------------------------------------
// unsubscribe(id)
// -----------------------------------------------------------------------------
void EventBus::unsubscribe(SubscriptionId id) {
  std::lock_guard lock(mutex_);

  // std::remove_if: moves entries that do NOT match the id to the front,
  // returns new logical end. Does not shrink the vector; erase() removes the
  // "removed" elements from the container. Lambda captures id by value so we
  // can use it inside remove_if safely.
  subscribers_.erase(
      std::remove_if(subscribers_.begin(), subscribers_.end(),
                     [id](const SubscriberEntry& e) { return e.first == id; }),
      subscribers_.end());
}

// -----------------------------------------------------------------------------
// publish(event)
// -----------------------------------------------------------------------------
void EventBus::publish(const Event& event) {
  std::vector<SubscriberEntry> copy;

  {
    // Hold the lock only while copying the subscriber list. This way we do not
    // call user callbacks under the lock. If a callback calls publish() or
    // unsubscribe(), we avoid deadlock. Callbacks may see a slightly stale
    // subscriber list (e.g. a just-added subscriber might not get this event);
    // that is acceptable for this design.
    std::lock_guard lock(mutex_);
    copy = subscribers_;
  }

  // Invoke every registered callback. Structured binding [id, callback] gives
  // readable names; we only use callback here. id could be used for logging.
  for (const auto& [id, callback] : copy) {
    callback(event);
  }
}

}  // namespace quant
