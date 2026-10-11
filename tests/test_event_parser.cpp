#include "event_parser.hpp"
#include "map_order_book.hpp"
#include <cassert>
#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, label)                                              \
    do {                                                                \
        if (cond) { ++g_pass; }                                         \
        else {                                                          \
            ++g_fail;                                                   \
            std::fprintf(stderr, "[FAIL] %s  (line %d)\n", label, __LINE__); \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b, label)                                           \
    do {                                                                \
        auto _a = (a); auto _b = (b);                                   \
        if (_a == _b) { ++g_pass; }                                     \
        else {                                                          \
            ++g_fail;                                                   \
            std::fprintf(stderr, "[FAIL] %s  got=%lld expected=%lld  (line %d)\n", \
                         label, (long long)_a, (long long)_b, __LINE__); \
        }                                                               \
    } while (0)

static constexpr int64_t S = 100'000'000LL; // 1 unit in ticks

static lob::core::MapOrderBook fresh_book() { return lob::core::MapOrderBook{}; }

static void test_depth_diff_bids_and_asks() {
    // Real payload from btcusdt@depth@100ms (inner data object only)
    const char* json = R"({
        "e":"depthUpdate","E":1791469327014,"s":"BTCUSDT",
        "U":101196507535,"u":101196507575,
        "b":[["82629.26000000","1.13712000"],["82624.00000000","0.01000000"]],
        "a":[["82634.00000000","2.50000000"],["82635.00000000","1.00000000"]]
    })";

    auto book = fresh_book();
    lob::parser::EventParser parser;
    auto r = parser.parse(lob::core::StreamKind::DepthDiff, json, book);

    CHECK(r.ok,                "depth_diff: ok");
    CHECK_EQ(r.type_char, 'D', "depth_diff: type_char");
    CHECK_EQ(r.side_char, 'N', "depth_diff: side_char N (both sides)");

    CHECK_EQ(book.bid_depth(), 2u, "depth_diff: 2 bid levels inserted");
    CHECK_EQ(book.ask_depth(), 2u, "depth_diff: 2 ask levels inserted");

    auto t = book.top5();
    // Best bid = 82629.26
    CHECK_EQ(t.bids[0].price, 8'262'926'000'000LL, "depth_diff: best bid price");
    CHECK_EQ(t.bids[0].qty,     113'712'000LL,      "depth_diff: best bid qty");
    // Best ask = 82634.00
    CHECK_EQ(t.asks[0].price, 8'263'400'000'000LL, "depth_diff: best ask price");
}

static void test_depth_diff_qty_zero_removes() {
    const char* setup = R"({
        "e":"depthUpdate","E":1,"s":"BTCUSDT","U":1,"u":2,
        "b":[["100.00000000","5.00000000"],["99.00000000","3.00000000"]],
        "a":[["101.00000000","2.00000000"]]
    })";
    const char* remove_top = R"({
        "e":"depthUpdate","E":2,"s":"BTCUSDT","U":3,"u":4,
        "b":[["100.00000000","0.00000000"]],
        "a":[]
    })";

    auto book = fresh_book();
    lob::parser::EventParser parser;

    parser.parse(lob::core::StreamKind::DepthDiff, setup, book);
    CHECK_EQ(book.bid_depth(), 2u, "before removal: 2 bids");

    auto r = parser.parse(lob::core::StreamKind::DepthDiff, remove_top, book);
    CHECK(r.ok, "remove: parse ok");
    CHECK_EQ(book.bid_depth(), 1u, "after qty=0: 1 bid left");

    auto t = book.top5();
    CHECK_EQ(t.bids[0].price, 99 * S, "after removal: best bid is now 99");
}

static void test_depth_diff_bid_only_side() {
    // Only "b" array, no "a" → side should be 'B'
    const char* json = R"({
        "e":"depthUpdate","E":1,"s":"BTCUSDT","U":1,"u":1,
        "b":[["100.00000000","1.00000000"]],
        "a":[]
    })";
    auto book = fresh_book();
    lob::parser::EventParser parser;
    auto r = parser.parse(lob::core::StreamKind::DepthDiff, json, book);
    CHECK_EQ(r.side_char, 'B', "bid-only: side_char B");
}

static void test_depth_diff_ask_only_side() {
    // Only "a" array, no "b" → side should be 'A'
    const char* json = R"({
        "e":"depthUpdate","E":1,"s":"BTCUSDT","U":1,"u":1,
        "b":[],
        "a":[["101.00000000","1.00000000"]]
    })";
    auto book = fresh_book();
    lob::parser::EventParser parser;
    auto r = parser.parse(lob::core::StreamKind::DepthDiff, json, book);
    CHECK_EQ(r.side_char, 'S', "ask-only: side_char S");
}

static void test_depth5_replaces_book() {
    // First put some stale state in the book
    auto book = fresh_book();
    book.update_bid(200 * S, 1 * S);  // should be gone after snapshot
    book.update_ask(201 * S, 1 * S);  // should be gone after snapshot

    // Real payload from btcusdt@depth5@100ms (inner data object only)
    const char* json = R"({
        "lastUpdateId":101196507597,
        "bids":[
            ["82633.95000000","6.97443000"],
            ["82633.94000000","0.00013000"],
            ["82633.93000000","0.00013000"],
            ["82633.83000000","0.00008000"],
            ["82633.00000000","0.10000000"]
        ],
        "asks":[
            ["82634.00000000","1.23000000"],
            ["82635.00000000","2.00000000"],
            ["82636.00000000","3.00000000"],
            ["82637.00000000","4.00000000"],
            ["82638.00000000","5.00000000"]
        ]
    })";

    lob::parser::EventParser parser;
    auto r = parser.parse(lob::core::StreamKind::Depth5, json, book);

    CHECK(r.ok,                "depth5: ok");
    CHECK_EQ(r.type_char, 'S', "depth5: type_char S");
    CHECK_EQ(r.side_char, 'N', "depth5: side_char N");

    CHECK_EQ(book.bid_depth(), 5u, "depth5: 5 bid levels");
    CHECK_EQ(book.ask_depth(), 5u, "depth5: 5 ask levels");

    auto t = book.top5();
    // Old stale level 200 must be gone
    CHECK_EQ(t.bids[0].price, 8'263'395'000'000LL, "depth5: best bid price");
    CHECK_EQ(t.bids[0].qty,     697'443'000LL,      "depth5: best bid qty");
    CHECK_EQ(t.asks[0].price, 8'263'400'000'000LL, "depth5: best ask price");
}

static void test_depth5_type_and_side() {
    const char* json = R"({
        "lastUpdateId":1,
        "bids":[["100.00000000","1.00000000"]],
        "asks":[["101.00000000","2.00000000"]]
    })";
    auto book = fresh_book();
    lob::parser::EventParser parser;
    auto r = parser.parse(lob::core::StreamKind::Depth5, json, book);
    CHECK_EQ(r.type_char, 'S', "depth5 type=S");
    CHECK_EQ(r.side_char, 'N', "depth5 side=N (always symmetric)");
}

static void test_trade_buy_side() {
    // m=false → buyer is taker → buy trade → side='B'
    const char* json = R"({
        "e":"trade","E":1791469327227,"s":"BTCUSDT",
        "t":6747947383,
        "p":"82633.96000000",
        "q":"0.00144000",
        "T":1791469327227,
        "m":false,
        "M":true
    })";

    auto book = fresh_book();
    lob::parser::EventParser parser;
    auto r = parser.parse(lob::core::StreamKind::Trade, json, book);

    CHECK(r.ok,                "trade buy: ok");
    CHECK_EQ(r.type_char, 'T', "trade buy: type_char T");
    CHECK_EQ(r.side_char, 'B', "trade buy: side_char B (buyer is taker)");

    CHECK_EQ(r.trade_price, 8'263'396'000'000LL, "trade buy: price ticks");
    CHECK_EQ(r.trade_qty,              144'000LL, "trade buy: qty ticks");
    CHECK(r.buyer_is_maker == false,   "trade buy: buyer_is_maker false");

    // Trade must NOT modify the book
    CHECK_EQ(book.bid_depth(), 0u, "trade: book unchanged (bid)");
    CHECK_EQ(book.ask_depth(), 0u, "trade: book unchanged (ask)");
}

static void test_trade_sell_side() {
    // m=true → buyer is market maker → seller is taker → sell trade → side='A'
    const char* json = R"({
        "e":"trade","E":1791469327637,"s":"BTCUSDT",
        "t":6747947385,
        "p":"82633.95000000",
        "q":"0.00063000",
        "T":1791469327637,
        "m":true,
        "M":true
    })";

    auto book = fresh_book();
    lob::parser::EventParser parser;
    auto r = parser.parse(lob::core::StreamKind::Trade, json, book);

    CHECK(r.ok,                "trade sell: ok");
    CHECK_EQ(r.type_char, 'T', "trade sell: type_char T");
    CHECK_EQ(r.side_char, 'S', "trade sell: side_char S (seller is taker)");
    CHECK(r.buyer_is_maker,    "trade sell: buyer_is_maker true");
}

static void test_malformed_json_returns_not_ok() {
    auto book = fresh_book();
    lob::parser::EventParser parser;

    auto r1 = parser.parse(lob::core::StreamKind::DepthDiff, "", book);
    CHECK(!r1.ok, "empty string: not ok");

    auto r2 = parser.parse(lob::core::StreamKind::DepthDiff, "not json {{{", book);
    CHECK(!r2.ok, "malformed JSON: not ok");

    auto r3 = parser.parse(lob::core::StreamKind::DepthDiff, "[1,2,3]", book);
    CHECK(!r3.ok, "JSON array (not object): not ok");
}

static void test_parser_is_reusable_across_calls() {
    // The document is reused — verify a second call doesn't bleed state
    auto book = fresh_book();
    lob::parser::EventParser parser;

    const char* j1 = R"({"e":"depthUpdate","E":1,"s":"X","U":1,"u":1,
                          "b":[["100.00000000","5.00000000"]],"a":[]})";
    const char* j2 = R"({"e":"depthUpdate","E":2,"s":"X","U":2,"u":2,
                          "b":[["99.00000000","3.00000000"]],"a":[]})";

    parser.parse(lob::core::StreamKind::DepthDiff, j1, book);
    parser.parse(lob::core::StreamKind::DepthDiff, j2, book);

    CHECK_EQ(book.bid_depth(), 2u, "reuse: both levels applied");
    CHECK_EQ(book.top5().bids[0].price, 100 * S, "reuse: best bid still 100");
    CHECK_EQ(book.top5().bids[1].price,  99 * S, "reuse: 2nd bid is 99");
}

static void test_stream_kind_from_name() {
    using lob::core::stream_kind_from_name;
    using lob::core::StreamKind;
    CHECK(stream_kind_from_name("btcusdt@depth@100ms")  == StreamKind::DepthDiff, "kind: depth@100ms");
    CHECK(stream_kind_from_name("btcusdt@depth5@100ms") == StreamKind::Depth5,    "kind: depth5@100ms");
    CHECK(stream_kind_from_name("btcusdt@trade")        == StreamKind::Trade,     "kind: @trade");
    CHECK(stream_kind_from_name("ethusdt@depth5@100ms") == StreamKind::Depth5,    "kind: eth depth5");
}

int main() {
    std::puts("=== Module 3: Event Parser Tests ===\n");

    test_depth_diff_bids_and_asks();
    test_depth_diff_qty_zero_removes();
    test_depth_diff_bid_only_side();
    test_depth_diff_ask_only_side();
    test_depth5_replaces_book();
    test_depth5_type_and_side();
    test_trade_buy_side();
    test_trade_sell_side();
    test_malformed_json_returns_not_ok();
    test_parser_is_reusable_across_calls();
    test_stream_kind_from_name();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
