# Task 01 — Phase 2: Python Backtest Data Feeder

## Purpose

The Python backtest feeder is a standalone script that acts as a **Mock Exchange**. It reads historical tick data from a CSV file and publishes each row as a JSON message over a ZeroMQ PUB socket. The C++ `MarketDataGateway` (ZeroMQ SUB) connects to this socket, parses the JSON, advances the simulation clock, and injects the data into the engine's event pipeline.

This architecture completely decouples data ingestion from C++ execution logic. By piping historical data over ZeroMQ IPC, we mimic the exact network behavior of a live WebSocket connection without HTTP overhead, allowing safe testing of the production C++ binary against recorded market data.

---

## ZeroMQ JSON Wire Schema

Every message sent by the Python feeder must be a UTF-8 JSON string with exactly these four fields:

```json
{
    "timestamp_ms": 1700000000000,
    "symbol":       "AAPL",
    "price":        150.25,
    "volume":       200.0
}
```

| Field          | Type    | Description                                      |
|----------------|---------|--------------------------------------------------|
| `timestamp_ms` | int64   | Epoch milliseconds (UTC). Must be sequential across ticks for correct simulation clock progression. |
| `symbol`       | string  | Instrument identifier (e.g. `"AAPL"`, `"ES"`).   |
| `price`        | float64 | Last or mid price for this tick.                  |
| `volume`       | float64 | Tick volume or quantity.                          |

**Contract enforcement:** The C++ `MarketDataGateway` uses `json.at("field_name")` for extraction, which throws (and logs to stderr) if any field is missing. Extra fields are silently ignored.

---

## File Structure

```
tools/backtest_feeder/
├── feeder.py          # ZeroMQ PUB publisher script
└── dummy_data.csv     # 5-row sample tick data for initial trials
```

---

## Python Dependencies

Only one external package is required:

```
pip install pyzmq
```

The script uses only `zmq`, `csv`, `json`, `time`, `os`, `sys`, and `argparse` — all of which (except `zmq`) are in the Python standard library.

---

## How to Run the Trial

### Prerequisites

1. **ZeroMQ C library** installed on the system (`brew install zeromq` on macOS).
2. **pyzmq** installed in Python (`pip install pyzmq`).
3. **C++ engine** built successfully (`cmake -S . -B build && cmake --build build`).

### Step-by-step

Open **two terminal windows** from the project root (`quant-core-engine/`).

**Terminal 1 — Start the C++ engine:**

```bash
./build/quant_engine
```

The engine starts in Simulation Mode: creates a `SimulationTimeProvider`, spawns strategy and risk/execution threads, and the `MarketDataGateway` begins listening on `tcp://127.0.0.1:5555` (SUB socket). The main thread blocks in the gateway's recv loop, waiting for ticks from the Python feeder. Press Ctrl-C for a clean shutdown.

**Terminal 2 — Start the Python feeder:**

```bash
python tools/backtest_feeder/feeder.py
```

The feeder:
1. Loads `dummy_data.csv` (5 ticks).
2. Binds a ZMQ PUB socket to `tcp://127.0.0.1:5555`.
3. Waits 1 second for the C++ SUB socket to complete the TCP handshake (mitigates ZeroMQ's "slow joiner" problem).
4. Sends each tick as a JSON string with a 10 ms delay between sends.
5. Prints each sent message to stdout.

**Expected feeder output:**

```
[feeder] loaded 5 ticks from tools/backtest_feeder/dummy_data.csv
[feeder] PUB socket bound to tcp://127.0.0.1:5555
[feeder] waiting 1.0s for subscribers to connect...
[feeder] sent 1/5: {"timestamp_ms": 1700000000000, "symbol": "AAPL", "price": 150.25, "volume": 200.0}
[feeder] sent 2/5: {"timestamp_ms": 1700000001000, "symbol": "AAPL", "price": 150.5, "volume": 350.0}
[feeder] sent 3/5: {"timestamp_ms": 1700000002000, "symbol": "AAPL", "price": 149.75, "volume": 125.0}
[feeder] sent 4/5: {"timestamp_ms": 1700000003000, "symbol": "AAPL", "price": 151.0, "volume": 500.0}
[feeder] sent 5/5: {"timestamp_ms": 1700000004000, "symbol": "AAPL", "price": 150.8, "volume": 275.0}
[feeder] all ticks sent. exiting.
```

### Custom data

```bash
python tools/backtest_feeder/feeder.py --file /path/to/your/data.csv
```

The CSV must have the header `timestamp_ms,symbol,price,volume` and numeric types must be parseable as int/float.

---

## Architecture Alignment

- **Separation of concerns:** The Python feeder is a completely separate process. It knows nothing about the C++ engine's internals — only the JSON wire format.
- **No global mutable state:** The feeder is stateless after loading the CSV.
- **Event-driven communication:** Data enters the engine via `MarketDataEvent` through the `EventBus`, exactly as a live market data feed would.
- **Thread model:** The `MarketDataGateway` runs on a dedicated C++ thread (the "Market Data Thread" from `architecture.md`). The feeder runs in its own Python process.
- **ZeroMQ PUB/SUB pattern:** Mirrors production topology where a market data server publishes and the engine subscribes, making the transition from backtest to live a configuration change rather than a code change.
