#include "quant/engine/trading_engine.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <utility>

namespace quant {

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TradingEngine::TradingEngine(SimulationTimeProvider& sim_clock,
                             std::string market_data_endpoint,
                             std::string ipc_cmd_endpoint,
                             std::string ipc_pub_endpoint)
    : sim_clock_(sim_clock),
      market_data_endpoint_(std::move(market_data_endpoint)),
      ipc_cmd_endpoint_(std::move(ipc_cmd_endpoint)),
      ipc_pub_endpoint_(std::move(ipc_pub_endpoint)) {}

// -----------------------------------------------------------------------------
// Destructor: RAII stop
// -----------------------------------------------------------------------------
TradingEngine::~TradingEngine() { stop(); }

// -----------------------------------------------------------------------------
// start()
// -----------------------------------------------------------------------------
void TradingEngine::start(IReconciler* reconciler) {
  if (running_) {
    return;
  }

  // ---  1) Create stateful components FIRST (before event loops start) ------
  order_tracker_ =
      std::make_unique<OrderTracker>(risk_loop_.eventBus());
  position_engine_ =
      std::make_unique<PositionEngine>(risk_loop_.eventBus(), risk_limits_);

  // ---  2) Synchronization Gate (optional) ----------------------------------
  if (reconciler != nullptr) {
    auto positions = reconciler->reconcilePositions();
    for (const auto& pos : positions) {
      position_engine_->hydratePosition(pos);
    }

    auto orders = reconciler->reconcileOrders();
    for (const auto& order : orders) {
      order_tracker_->hydrateOrder(order);
    }

    std::cout << "[TradingEngine] Reconciliation complete: "
              << positions.size() << " position(s), "
              << orders.size() << " open order(s) hydrated.\n";
  }

  // ---  3) Start core event loops (spawns worker threads) -------------------
  strategy_loop_.start();
  risk_loop_.start();

  // ---  4) Wire cross-thread bridges ----------------------------------------

  // Bridge 1: SignalEvent from strategy_loop → risk_loop
  strategy_loop_.eventBus().subscribe<SignalEvent>(
      [this](const SignalEvent& e) { risk_loop_.push(e); });

  // ---  5) Start OrderRoutingThread -----------------------------------------
  order_routing_thread_ =
      std::make_unique<OrderRoutingThread>(&sim_clock_);
  order_routing_thread_->start();

  // Bridge 2: OrderEvent from risk_loop → order_routing_thread
  risk_loop_.eventBus().subscribe<OrderEvent>(
      [this](const OrderEvent& e) { order_routing_thread_->push(e); });

  // Bridge 3: ExecutionReportEvent from order_routing_thread → risk_loop
  order_routing_thread_->eventBus().subscribe<ExecutionReportEvent>(
      [this](const ExecutionReportEvent& e) { risk_loop_.push(e); });

  // ---  6) Create remaining logic components --------------------------------
  strategy_ = std::make_unique<DummyStrategy>(strategy_loop_.eventBus());
  risk_engine_ =
      std::make_unique<RiskEngine>(risk_loop_.eventBus(),
                                   order_id_gen_, *position_engine_,
                                   risk_limits_);

  // ---  7) Start IpcServer (telemetry + commands) ----------------------------
  if (!ipc_cmd_endpoint_.empty() && !ipc_pub_endpoint_.empty()) {
    ipc_server_ = std::make_unique<IpcServer>(
        [this](const std::string& cmd) { return executeCommand(cmd); },
        ipc_cmd_endpoint_, ipc_pub_endpoint_);
    ipc_server_->start();

    // Telemetry bridges: forward events from risk_loop to IPC server queue.
    risk_loop_.eventBus().subscribe<OrderUpdateEvent>(
        [this](const OrderUpdateEvent& e) { ipc_server_->pushTelemetry(e); });
    risk_loop_.eventBus().subscribe<PositionUpdateEvent>(
        [this](const PositionUpdateEvent& e) {
          ipc_server_->pushTelemetry(e);
        });
    risk_loop_.eventBus().subscribe<RiskViolationEvent>(
        [this](const RiskViolationEvent& e) {
          ipc_server_->pushTelemetry(e);
        });
  }

  // ---  8) Start MarketDataThread LAST (ticks begin flowing) ----------------
  // Skip if no endpoint was configured (unit tests push events manually).
  if (!market_data_endpoint_.empty()) {
    market_data_thread_ = std::make_unique<MarketDataThread>(
        sim_clock_,
        [this](Event event) { pushEvent(std::move(event)); },
        market_data_endpoint_);
    market_data_thread_->start();
  }

  running_ = true;

  std::cout << "[TradingEngine] started. Threads: strategy, risk, "
               "order_routing"
            << (market_data_thread_ ? ", market_data" : "")
            << ".\n";
}

// -----------------------------------------------------------------------------
// stop()
// -----------------------------------------------------------------------------
void TradingEngine::stop() {
  if (!running_) {
    return;
  }

  // ---  1) Stop market data inflow FIRST ------------------------------------
  market_data_thread_.reset();

  // ---  1b) Stop IPC server (joins its thread before components are
  //          destroyed, since executeCommand() queries them) -----------------
  ipc_server_.reset();

  // ---  2) Destroy logic components (they unsubscribe from their buses) -----
  risk_engine_.reset();
  position_engine_.reset();
  order_tracker_.reset();
  strategy_.reset();

  // ---  3) Stop OrderRoutingThread (destroys ExecutionEngine, joins) ---------
  order_routing_thread_.reset();

  // ---  4) Stop core event loops (joins worker threads) ---------------------
  strategy_loop_.stop();
  risk_loop_.stop();

  running_ = false;

  std::cout << "[TradingEngine] stopped. All threads joined.\n";
}

// -----------------------------------------------------------------------------
// pushMarketData(event)
// -----------------------------------------------------------------------------
void TradingEngine::pushMarketData(MarketDataEvent event) {
  strategy_loop_.push(std::move(event));
}

// -----------------------------------------------------------------------------
// pushEvent(event)
// -----------------------------------------------------------------------------
void TradingEngine::pushEvent(Event event) {
  strategy_loop_.push(std::move(event));
}

// -----------------------------------------------------------------------------
// executeCommand(): handle IPC command requests
// -----------------------------------------------------------------------------
std::string TradingEngine::executeCommand(const std::string& cmd) {
  nlohmann::json response;

  if (cmd == "PING") {
    response["status"] = "ok";
    response["response"] = "PONG";
  } else if (cmd == "STATUS") {
    response["status"] = "ok";
    response["halted"] = risk_engine_ ? risk_engine_->isHalted() : false;

    nlohmann::json positions_json = nlohmann::json::array();
    if (position_engine_) {
      for (const auto& pos : position_engine_->getSnapshots()) {
        nlohmann::json p;
        p["symbol"] = pos.symbol;
        p["net_quantity"] = pos.net_quantity;
        p["average_price"] = pos.average_price;
        p["realized_pnl"] = pos.realized_pnl;
        positions_json.push_back(std::move(p));
      }
    }
    response["positions"] = std::move(positions_json);
  } else if (cmd == "HALT") {
    if (risk_engine_) {
      risk_engine_->haltTrading();
    }
    response["status"] = "ok";
    response["response"] = "Trading halted";
  } else {
    response["status"] = "error";
    response["response"] = "Unknown command: " + cmd;
  }

  return response.dump();
}

// -----------------------------------------------------------------------------
// EventBus accessors
// -----------------------------------------------------------------------------
EventBus& TradingEngine::strategyEventBus() {
  return strategy_loop_.eventBus();
}

EventBus& TradingEngine::riskExecutionEventBus() {
  return risk_loop_.eventBus();
}

}  // namespace quant
