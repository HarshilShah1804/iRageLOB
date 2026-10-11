#include "replay_engine.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, label)                                                  \
    do {                                                                    \
        if (cond) { ++g_pass; }                                             \
        else { ++g_fail; std::fprintf(stderr, "[FAIL] %s  (line %d)\n",    \
                                      label, __LINE__); }                   \
    } while (0)

#define CHECK_EQ(a, b, label)                                               \
    do {                                                                    \
        std::string _a = (a); std::string _b = (b);                         \
        if (_a == _b) { ++g_pass; }                                         \
        else { ++g_fail; std::fprintf(stderr,                               \
            "[FAIL] %s  got=\"%s\" expected=\"%s\"  (line %d)\n",           \
            label, _a.c_str(), _b.c_str(), __LINE__); }                     \
    } while (0)


static void test_replay_end_to_end() {
    const std::string in_file  = "test_market_data.csv";
    const std::string out_file = "test_orderbook.csv";

    // 1. Write a dummy Deliverable A file
    {
        std::ofstream out(in_file);
        // Header
        out << "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json\n";

        // Row 1: depth5 snapshot (seq_no 42)
        // RFC 4180 escaped
        out << "100,500,spot,depth5,0,1,42,BTCUSDT,"
            << R"("{""lastUpdateId"":1,""bids"":[[""100.00"",""5.0""]],""asks"":[[""101.00"",""2.0""]]}")" << "\n";

        // Row 2: trade (seq_no 43)
        out << "100,600,spot,trade,0,1,43,BTCUSDT,"
            << R"("{""e"":""trade"",""p"":""100.00"",""q"":""1.0"",""m"":false}")" << "\n";
    }

    // 2. Run the replay engine
    bool ok = lob::replay::replay_market_data(in_file, out_file);
    CHECK(ok, "replay_engine: ran successfully");

    // 3. Verify the Deliverable B output file
    {
        std::ifstream in(out_file);
        std::string line;

        // Header check
        std::getline(in, line);
        CHECK_EQ(line, "tsec,tnsec,seqNo,id,type,side,bid0,bid1,bid2,bid3,bid4,bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,ask0,ask1,ask2,ask3,ask4,ask_size0,ask_size1,ask_size2,ask_size3,ask_size4", "header matches perfectly");

        // Row 1 check (depth5 snapshot)
        std::getline(in, line);
        // Expected:
        // tsec=100, tnsec=500, seqNo=42, id=1, type=S, side=N
        // bid0=10000000000, qty=500000000
        // ask0=10100000000, qty=200000000
        // (all other levels padded with 0)
        CHECK_EQ(line, "100,500,42,1,S,N,10000000000,0,0,0,0,500000000,0,0,0,0,10100000000,0,0,0,0,200000000,0,0,0,0", "row 1 (depth5) exact match");

        // Row 2 check (trade)
        std::getline(in, line);
        // Expected:
        // tsec=100, tnsec=600, seqNo=43, id=2, type=T, side=B
        // Levels remain identical to Row 1
        CHECK_EQ(line, "100,600,43,2,T,B,10000000000,0,0,0,0,500000000,0,0,0,0,10100000000,0,0,0,0,200000000,0,0,0,0", "row 2 (trade) exact match");

        // No more lines
        CHECK(!std::getline(in, line), "no extra rows");
    }

    // Cleanup
    std::remove(in_file.c_str());
    std::remove(out_file.c_str());
}

int main() {
    std::puts("=== Module 5: Replay Engine Tests ===\n");

    test_replay_end_to_end();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
