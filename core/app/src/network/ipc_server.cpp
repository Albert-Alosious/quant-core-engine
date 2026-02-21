#include "quant/network/ipc_server.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <utility>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor: store parameters for deferred socket creation
// -----------------------------------------------------------------------------
IpcServer::IpcServer(CommandHandler command_handler,
                     std::string cmd_endpoint,
                     std::string pub_endpoint)
    : command_handler_(std::move(command_handler)),
      cmd_endpoint_(std::move(cmd_endpoint)),
      pub_endpoint_(std::move(pub_endpoint)) {}

// -----------------------------------------------------------------------------
// Destructor: RAII stop
// -----------------------------------------------------------------------------
IpcServer::~IpcServer() { stop(); }

// -----------------------------------------------------------------------------
// start(): create sockets and spawn worker thread
// -----------------------------------------------------------------------------
void IpcServer::start() {
  if (running_.load()) {
    return;
  }

  context_ = std::make_unique<zmq::context_t>(1);
  cmd_socket_ =
      std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::rep);
  pub_socket_ =
      std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::pub);

  cmd_socket_->set(zmq::sockopt::rcvtimeo, kPollTimeoutMs);
  cmd_socket_->bind(cmd_endpoint_);
  pub_socket_->bind(pub_endpoint_);

  running_.store(true);

  thread_ = std::thread([this] { run(); });

  std::cout << "[IpcServer] started. CMD=" << cmd_endpoint_
            << " PUB=" << pub_endpoint_ << "\n";
}

// -----------------------------------------------------------------------------
// stop(): signal and join
// -----------------------------------------------------------------------------
void IpcServer::stop() {
  if (!running_.load()) {
    if (thread_.joinable()) {
      thread_.join();
    }
    return;
  }

  running_.store(false);

  if (thread_.joinable()) {
    thread_.join();
  }

  cmd_socket_.reset();
  pub_socket_.reset();
  context_.reset();

  std::cout << "[IpcServer] stopped.\n";
}

// -----------------------------------------------------------------------------
// pushTelemetry(): thread-safe enqueue from risk_loop
// -----------------------------------------------------------------------------
void IpcServer::pushTelemetry(Event event) {
  telemetry_queue_.push(std::move(event));
}

// -----------------------------------------------------------------------------
// run(): combined poll/drain loop
// -----------------------------------------------------------------------------
void IpcServer::run() {
  while (running_.load()) {
    processTelemetry();
    processCommands();
  }

  // Final drain: publish any remaining telemetry before shutdown.
  processTelemetry();
}

// -----------------------------------------------------------------------------
// processTelemetry(): drain queue and publish JSON on PUB socket
// -----------------------------------------------------------------------------
void IpcServer::processTelemetry() {
  while (auto maybe_event = telemetry_queue_.try_pop()) {
    auto json_str = formatTelemetry(*maybe_event);
    if (json_str.has_value()) {
      zmq::message_t msg(json_str->data(), json_str->size());
      pub_socket_->send(msg, zmq::send_flags::dontwait);
    }
  }
}

// -----------------------------------------------------------------------------
// processCommands(): poll REP socket and dispatch
// -----------------------------------------------------------------------------
void IpcServer::processCommands() {
  zmq::message_t request;
  zmq::recv_result_t result;

  try {
    result = cmd_socket_->recv(request, zmq::recv_flags::none);
  } catch (const zmq::error_t& e) {
    if (e.num() == EINTR) {
      return;
    }
    throw;
  }

  if (!result.has_value()) {
    return;
  }

  std::string cmd(static_cast<const char*>(request.data()), request.size());
  std::string response = command_handler_(cmd);

  zmq::message_t reply(response.data(), response.size());
  cmd_socket_->send(reply, zmq::send_flags::none);
}

// -----------------------------------------------------------------------------
// formatTelemetry(): dispatch Event variant to per-type formatters
// -----------------------------------------------------------------------------
std::optional<std::string> IpcServer::formatTelemetry(const Event& event) {
  if (auto* e = std::get_if<OrderUpdateEvent>(&event)) {
    return formatOrderUpdate(*e);
  }
  if (auto* e = std::get_if<PositionUpdateEvent>(&event)) {
    return formatPositionUpdate(*e);
  }
  if (auto* e = std::get_if<RiskViolationEvent>(&event)) {
    return formatRiskViolation(*e);
  }
  return std::nullopt;
}

// -----------------------------------------------------------------------------
// formatOrderUpdate()
// -----------------------------------------------------------------------------
std::string IpcServer::formatOrderUpdate(const OrderUpdateEvent& e) {
  nlohmann::json j;
  j["type"] = "order_update";
  j["order_id"] = e.order.id;
  j["symbol"] = e.order.symbol;
  j["side"] = sideToString(e.order.side);
  j["status"] = orderStatusToString(e.order.status);
  j["previous_status"] = orderStatusToString(e.previous_status);
  j["quantity"] = e.order.quantity;
  j["price"] = e.order.price;
  j["filled_quantity"] = e.order.filled_quantity;
  return j.dump();
}

// -----------------------------------------------------------------------------
// formatPositionUpdate()
// -----------------------------------------------------------------------------
std::string IpcServer::formatPositionUpdate(const PositionUpdateEvent& e) {
  nlohmann::json j;
  j["type"] = "position_update";
  j["symbol"] = e.position.symbol;
  j["net_quantity"] = e.position.net_quantity;
  j["average_price"] = e.position.average_price;
  j["realized_pnl"] = e.position.realized_pnl;
  return j.dump();
}

// -----------------------------------------------------------------------------
// formatRiskViolation()
// -----------------------------------------------------------------------------
std::string IpcServer::formatRiskViolation(const RiskViolationEvent& e) {
  nlohmann::json j;
  j["type"] = "risk_violation";
  j["symbol"] = e.symbol;
  j["reason"] = e.reason;
  j["current_value"] = e.current_value;
  j["limit_value"] = e.limit_value;
  return j.dump();
}

// -----------------------------------------------------------------------------
// orderStatusToString()
// -----------------------------------------------------------------------------
const char* IpcServer::orderStatusToString(domain::OrderStatus s) {
  using S = domain::OrderStatus;
  switch (s) {
    case S::New:             return "New";
    case S::PendingNew:      return "PendingNew";
    case S::Accepted:        return "Accepted";
    case S::PartiallyFilled: return "PartiallyFilled";
    case S::Filled:          return "Filled";
    case S::Canceled:        return "Canceled";
    case S::Rejected:        return "Rejected";
    case S::Expired:         return "Expired";
  }
  return "Unknown";
}

// -----------------------------------------------------------------------------
// sideToString()
// -----------------------------------------------------------------------------
const char* IpcServer::sideToString(domain::Side s) {
  switch (s) {
    case domain::Side::Buy:  return "Buy";
    case domain::Side::Sell: return "Sell";
  }
  return "Unknown";
}

}  // namespace quant
