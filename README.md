# Binance Order Book Capture (iRage Assignment)

A high-performance C++17 WebSocket client that connects to Binance streams, maintains a real-time Level 2 Limit Order Book (LOB), and deterministically captures state to CSV.

---

## Dependencies & Prerequisites

- **Ubuntu 22.04 / Debian** dependencies:
  ```bash
  sudo apt-get update
  sudo apt-get install -y cmake ninja-build g++-12 libssl-dev libboost-all-dev
  ```
- **CMake** >= 3.16
- **Boost** (system, beast, asio) >= 1.74
- **OpenSSL** (crypto, ssl)
- **RapidJSON** (vendored in `third_party/rapidjson`)

---

## Building

```bash
# Clone RapidJSON vendor if not present
git clone --depth 1 https://github.com/Tencent/rapidjson.git third_party/rapidjson

# Configure & Build
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-12 \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -O2 -Werror"
cmake --build build --parallel

# Run Unit Test Suite (118 assertions across 5 modules)
ctest --test-dir build --output-on-failure
```

---

## Usage

### 1. Live Capture Mode
Connects to Binance combined streams (`@depth@100ms`, `@depth5@100ms`, `@trade`), maintains the local LOB, and streams both deliverables to disk.

```bash
# Single symbol capture (Spot)
./build/binance_capture --venue spot --symbols BTCUSDT --duration 120 --output-dir ./output

# USD-M Futures capture
./build/binance_capture --venue usdm --symbols BTCUSDT --duration 120 --output-dir ./output

# Multi-symbol capture
./build/binance_capture --venue spot --symbols BTCUSDT,ETHUSDT --duration 60 --output-dir ./output
```

*Outputs produced in `--output-dir`:*
- **Deliverable A**: `market_data_spot_BTCUSDT_YYYY-MM-DD.csv`
- **Deliverable B**: `market_data_spot_BTCUSDT_YYYY-MM-DD_orderbook.csv`

*(Note: When running without `--output-dir`, convenience symlinks `market_data.csv` and `orderbook.csv` are also created in the root directory).*

### 2. Offline Parity (Replay Mode)
Demonstrates 100% deterministic replay from Deliverable A to regenerate Deliverable B with **zero network calls**:

```bash
# Run offline replay from saved market data
./build/binance_capture --replay market_data.csv orderbook_offline.csv

# Verify byte-identical parity (returns 0 diff)
diff -u orderbook.csv orderbook_offline.csv
```

---

## Mandatory Specification Policies

### 1. Timestamp Policy
- **Policy Chosen**: Process Wall-Clock Receive Time (the recommended policy).
- **Measurement**: Sampled immediately when the process finishes reading the complete WebSocket text frame from the socket (`std::chrono::system_clock::now()`).
- **Fields**:
  - `recv_tsec` (int64): Whole seconds since Unix epoch.
  - `recv_tnsec` (int32): Nanosecond remainder in $[0, 999\,999\,999]$ (`count() % 1'000'000'000`).
- **Deliverable B Parity**: The snapshot row timestamp (`tsec`, `tnsec`) inherits the exact receive timestamp of the triggering market data event, ensuring 1:1 temporal alignment between Deliverable A and Deliverable B.

### 2. File Naming Policy
- **Standard Convention**:
  - `market_data_<venue>_<SYMBOL>_<UTC-date>.csv` for Deliverable A.
  - `market_data_<venue>_<SYMBOL>_<UTC-date>_orderbook.csv` for Deliverable B.
- **Multi-Symbol Handling**: When multiple symbols are configured (e.g. `--symbols BTCUSDT,ETHUSDT`), the single-file-per-run format is used with the `symbol` column explicitly identifying the instrument for each row.

### 3. Fixed-Point Integer Scaling Policy
Binance sends decimal prices and quantities as ASCII strings (e.g. `"83034.01000000"`, `"0.02655000"`).
- **Scale Factor**: $10^8$ ticks scale (`SCALE = 100'000'000LL`).
- **Determinism**: Decimals are parsed directly from characters into `int64_t` fixed-point integers (`parse_fixed`).
- **Hot-Path Purity**: **Zero `double` or `float` floating-point conversions** are used on the hot path, preventing binary floating-point rounding errors and non-deterministic behavior.

### 4. Stream Semantics & LOB Resynchronization
- **`@depth@100ms` (Differential Depth)**: Updates resting price levels in the book. A quantity of `0` explicitly removes that price level.
- **`@depth5@100ms` (Partial Depth Snapshot)**: Replaces the top-5 levels on bid and ask sides. Acts as a continuous sanity check / top-of-book refresh.
- **`@trade` (Market Trades)**: Trades do not modify resting limit orders in the book; they annotate the current book state with trade price, trade quantity, and aggressor side (`side_char`).
- **Reconnects & Session Epoch**:
  - `conn_epoch`: Non-negative integer incremented by 1 on each WebSocket connection or reconnect.
  - `conn_seq`: Monotonic sequence number reset to 0 at the start of each epoch and incremented for each received frame.

---

## Architecture & Design Trade-offs

### 1. Data Structures: `IOrderBook` & `MapOrderBook`
The order book is abstracted behind an [`IOrderBook`](file:///home/harshil-shah/Downloads/iRage/include/order_book.hpp) interface:
- **Current Storage (`MapOrderBook`)**:
  - Implemented using two `std::map<int64_t, int64_t>` containers (`std::greater` for bids, `std::less` for asks).
  - *Pros:* Naturally sorted; finding top-5 is $O(1)$ (iterating first 5 nodes); memory usage is proportional only to populated levels.
  - *Cons:* Node allocations incur heap overhead; pointer chasing can cause CPU cache misses in ultra-low-latency environments.
- **Future Scaling / Low-Latency Migration**:
  - The interface allows zero-friction replacement with a **Direct Index Tick Array** (`int64_t[MAX_TICKS]`) for $O(1)$ updates and zero heap allocations.
  - In extreme HFT environments, `IOrderBook` can be swapped for static polymorphism (templates/concepts) to eliminate virtual dispatch overhead.

### 2. Allocation & Memory Efficiency
- **JSON Parsing**: Single reusable `rapidjson::Document` reused across events without heap reallocation.
- **CSV Formatting**: Output rows formatted using `std::to_chars` with pre-allocated `std::string` buffers (`reserve(4096)`), eliminating per-row heap allocations.
- **Single-Threaded Reactor Model**: Boost Beast WebSocket client runs on a single event loop, eliminating mutex contention and cache ping-pong on the critical path.

---

## Sample Run Outputs

Per the assignment specification (Page 3 & 5), streaming `*.csv` files are excluded from Git repository history via `.gitignore` to prevent repository bloat. 

The complete, full-length 90-second capture files (3,350 events) are attached directly to the **[v1.0.0 GitHub Release](../../releases/tag/v1.0.0)**:
- `market_data_spot_BTCUSDT_2026-10-11.csv` (Deliverable A — Audit Log)
- `market_data_spot_BTCUSDT_2026-10-11_orderbook.csv` (Deliverable B — Order Book Snapshots)

Below are truncated previews of each deliverable from that run:

### Deliverable A (`market_data_spot_BTCUSDT_2026-10-11.csv`)
```csv
recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json
1791749121,172109693,spot,depth5,0,1,1,BTCUSDT,"{""lastUpdateId"":101278912401,""bids"":[[""84100.02000000"",""3.38235000""],[""84100.01000000"",""0.12694000""]],""asks"":[[""84100.03000000"",""1.56346000""],[""84100.04000000"",""0.00047000""]]}"
1791749121,172193411,spot,depth_diff,0,1,2,BTCUSDT,"{""e"":""depthUpdate"",""E"":1791749121114,""s"":""BTCUSDT"",""U"":101278912398,""u"":101278912401,""b"":[],""a"":[[""84100.03000000"",""1.56346000""]]}"
```

### Deliverable B (`market_data_spot_BTCUSDT_2026-10-11_orderbook.csv`)
```csv
tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4
1791749121,172109693,1,1,S,N,8410002000000,8410001000000,8410000000000,8409998000000,8409975000000,338235000,12694000,20191000,705000,6000,8410003000000,8410004000000,8410005000000,8410035000000,8410118000000,156346000,47000,12000,8000,7000
1791749121,172193411,2,2,D,S,8410002000000,8410001000000,8410000000000,8409998000000,8409975000000,338235000,12694000,20191000,705000,6000,8410003000000,8410004000000,8410005000000,8410035000000,8410118000000,156346000,47000,12000,8000,7000
```

