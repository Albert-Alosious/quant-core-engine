#!/usr/bin/env python3
"""
Backtest Data Feeder — Mock Exchange for the Quant Core Engine.

Reads historical tick data from a CSV file and publishes each row as a
JSON message over a ZeroMQ PUB socket. The C++ MarketDataGateway (SUB)
connects to this socket, parses the JSON, and injects the data into the
engine's event pipeline.

JSON wire format (must match MarketDataGateway's expectations exactly):
    {
        "timestamp_ms": <int64>,   # epoch milliseconds
        "symbol":       <string>,  # instrument identifier
        "price":        <float>,   # last/mid price
        "volume":       <float>    # tick volume/quantity
    }

Usage:
    python feeder.py                          # uses default dummy_data.csv
    python feeder.py --file path/to/data.csv  # custom data file

Dependencies:
    pip install pyzmq
"""

import argparse
import csv
import json
import os
import sys
import time

import zmq


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

ENDPOINT = "tcp://127.0.0.1:5555"
INTER_TICK_DELAY_S = 0.01  # 10 ms between ticks for human-readable logs

# Default CSV path: dummy_data.csv in the same directory as this script.
DEFAULT_CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "dummy_data.csv")


def create_publisher(endpoint: str) -> tuple[zmq.Context, zmq.Socket]:
    """Create and bind a ZeroMQ PUB socket.

    The PUB socket is the server side of the PUB/SUB pattern. It binds to
    the endpoint and waits for subscribers (the C++ MarketDataGateway) to
    connect. Messages sent before any subscriber connects are silently
    dropped by ZeroMQ — this is expected behavior.

    Args:
        endpoint: ZMQ endpoint string, e.g. "tcp://127.0.0.1:5555".

    Returns:
        Tuple of (context, socket). Caller is responsible for cleanup.
    """
    ctx = zmq.Context()
    pub = ctx.socket(zmq.PUB)
    pub.bind(endpoint)
    return ctx, pub


def load_ticks(csv_path: str) -> list[dict]:
    """Load tick data from a CSV file.

    Expected CSV columns: timestamp_ms, symbol, price, volume.
    Types are converted from strings to their native Python types to match
    the JSON schema the C++ gateway expects.

    Args:
        csv_path: Path to the CSV file.

    Returns:
        List of dicts, each with keys: timestamp_ms (int), symbol (str),
        price (float), volume (float).

    Raises:
        FileNotFoundError: If csv_path does not exist.
        KeyError: If a required column is missing.
        ValueError: If a numeric field cannot be parsed.
    """
    ticks = []
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            ticks.append({
                "timestamp_ms": int(row["timestamp_ms"]),
                "symbol":       row["symbol"],
                "price":        float(row["price"]),
                "volume":       float(row["volume"]),
            })
    return ticks


def publish_ticks(pub: zmq.Socket, ticks: list[dict],
                  delay: float) -> None:
    """Publish each tick as a JSON string over the ZMQ PUB socket.

    A short delay between sends keeps the C++ engine's stdout logs
    human-readable during visual trials. In a real backtest replay this
    delay would be removed for maximum throughput.

    Args:
        pub:   Bound ZMQ PUB socket.
        ticks: List of tick dicts matching the JSON schema.
        delay: Seconds to sleep between each send.
    """
    for i, tick in enumerate(ticks, start=1):
        payload = json.dumps(tick)
        pub.send_string(payload)
        print(f"[feeder] sent {i}/{len(ticks)}: {payload}")
        time.sleep(delay)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Backtest Data Feeder — publishes CSV ticks over ZeroMQ.")
    parser.add_argument(
        "--file", default=DEFAULT_CSV,
        help="Path to the CSV data file (default: dummy_data.csv).")
    parser.add_argument(
        "--endpoint", default=ENDPOINT,
        help=f"ZMQ PUB endpoint (default: {ENDPOINT}).")
    parser.add_argument(
        "--delay", type=float, default=INTER_TICK_DELAY_S,
        help=f"Seconds between sends (default: {INTER_TICK_DELAY_S}).")
    args = parser.parse_args()

    # Validate CSV file exists before opening the socket.
    if not os.path.isfile(args.file):
        print(f"[feeder] ERROR: CSV file not found: {args.file}",
              file=sys.stderr)
        sys.exit(1)

    ticks = load_ticks(args.file)
    print(f"[feeder] loaded {len(ticks)} ticks from {args.file}")

    ctx, pub = create_publisher(args.endpoint)
    print(f"[feeder] PUB socket bound to {args.endpoint}")

    # Brief pause so the C++ SUB socket has time to complete the TCP
    # handshake. Without this, the first few messages may be dropped
    # because ZeroMQ PUB silently discards messages when no subscribers
    # are connected yet (the "slow joiner" problem).
    startup_delay = 1.0
    print(f"[feeder] waiting {startup_delay}s for subscribers to connect...")
    time.sleep(startup_delay)

    try:
        publish_ticks(pub, ticks, args.delay)
        print("[feeder] all ticks sent. exiting.")
    except KeyboardInterrupt:
        print("\n[feeder] interrupted by user.")
    finally:
        pub.close()
        ctx.term()


if __name__ == "__main__":
    main()
