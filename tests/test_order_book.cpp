#include "map_order_book.hpp"
#include <cassert>
#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, label)                                          \
    do {                                                            \
        if (cond) {                                                 \
            ++g_pass;                                               \
        } else {                                                    \
            ++g_fail;                                               \
            std::fprintf(stderr, "[FAIL] %s  (line %d)\n",         \
                         label, __LINE__);                          \
        }                                                           \
    } while (0)

#define CHECK_EQ(a, b, label)                                       \
    do {                                                            \
        auto _a = (a); auto _b = (b);                              \
        if (_a == _b) {                                             \
            ++g_pass;                                               \
        } else {                                                    \
            ++g_fail;                                               \
            std::fprintf(stderr, "[FAIL] %s  got=%lld expected=%lld  (line %d)\n", \
                         label, (long long)_a, (long long)_b, __LINE__); \
        }                                                           \
    } while (0)

// Prices in raw ticks (10^8 scale) to keep tests readable.
// e.g. price 100.00 = 10_000_000_000 ticks
static constexpr int64_t S = 100'000'000LL; // 1 unit in ticks

static lob::core::MapOrderBook make_book() { return lob::core::MapOrderBook{}; }

static void test_empty_book() {
    auto book = make_book();
    CHECK_EQ(book.bid_depth(), 0u, "empty bid depth");
    CHECK_EQ(book.ask_depth(), 0u, "empty ask depth");

    auto t = book.top5();
    // All slots must be zero when book is empty
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(t.bids[i].price, 0LL, "empty bids price");
        CHECK_EQ(t.bids[i].qty,   0LL, "empty bids qty");
        CHECK_EQ(t.asks[i].price, 0LL, "empty asks price");
        CHECK_EQ(t.asks[i].qty,   0LL, "empty asks qty");
    }
}

static void test_insert_and_update() {
    auto book = make_book();

    // Insert a bid level
    book.update_bid(100 * S, 5 * S);
    CHECK_EQ(book.bid_depth(), 1u, "bid depth after insert");
    CHECK_EQ(book.top5().bids[0].price, 100 * S, "bid price after insert");
    CHECK_EQ(book.top5().bids[0].qty,     5 * S, "bid qty after insert");

    // Update (overwrite) the same level with a new qty
    book.update_bid(100 * S, 9 * S);
    CHECK_EQ(book.bid_depth(), 1u, "bid depth after update (same level)");
    CHECK_EQ(book.top5().bids[0].qty, 9 * S, "bid qty after update");

    // Insert an ask level
    book.update_ask(101 * S, 3 * S);
    CHECK_EQ(book.ask_depth(), 1u, "ask depth after insert");
    CHECK_EQ(book.top5().asks[0].price, 101 * S, "ask price after insert");
}

static void test_qty_zero_removes_level() {
    auto book = make_book();

    book.update_bid(100 * S, 5 * S);
    book.update_bid(99  * S, 2 * S);
    CHECK_EQ(book.bid_depth(), 2u, "bid depth before removal");

    // qty = 0 → remove
    book.update_bid(100 * S, 0LL);
    CHECK_EQ(book.bid_depth(), 1u, "bid depth after qty=0 removal");
    CHECK_EQ(book.top5().bids[0].price, 99 * S, "best bid after top removed");

    // Removing a price that doesn't exist must be a no-op (not a crash)
    book.update_bid(999 * S, 0LL);
    CHECK_EQ(book.bid_depth(), 1u, "bid depth unchanged after removing non-existent level");

    // Same for asks
    book.update_ask(101 * S, 3 * S);
    book.update_ask(101 * S, 0LL);
    CHECK_EQ(book.ask_depth(), 0u, "ask depth after qty=0 removal");
}

static void test_bid_ordering_descending() {
    auto book = make_book();

    // Insert out-of-order; best bid must always be at [0]
    book.update_bid(99  * S, 1 * S);
    book.update_bid(101 * S, 2 * S);
    book.update_bid(100 * S, 3 * S);

    auto t = book.top5();
    CHECK_EQ(t.bids[0].price, 101 * S, "best bid is highest");
    CHECK_EQ(t.bids[1].price, 100 * S, "2nd bid");
    CHECK_EQ(t.bids[2].price,  99 * S, "3rd bid");
}

static void test_ask_ordering_ascending() {
    auto book = make_book();

    // Insert out-of-order; best ask must always be at [0]
    book.update_ask(103 * S, 1 * S);
    book.update_ask(101 * S, 2 * S);
    book.update_ask(102 * S, 3 * S);

    auto t = book.top5();
    CHECK_EQ(t.asks[0].price, 101 * S, "best ask is lowest");
    CHECK_EQ(t.asks[1].price, 102 * S, "2nd ask");
    CHECK_EQ(t.asks[2].price, 103 * S, "3rd ask");
}

static void test_top5_with_more_than_5_levels() {
    auto book = make_book();

    // Insert 8 bid levels
    for (int i = 1; i <= 8; ++i) {
        book.update_bid(i * 10 * S, i * S);
    }
    CHECK_EQ(book.bid_depth(), 8u, "8 bid levels inserted");

    auto t = book.top5();
    // Only the top 5 (highest prices) should appear
    CHECK_EQ(t.bids[0].price, 80 * S, "top5 bid[0]");
    CHECK_EQ(t.bids[1].price, 70 * S, "top5 bid[1]");
    CHECK_EQ(t.bids[2].price, 60 * S, "top5 bid[2]");
    CHECK_EQ(t.bids[3].price, 50 * S, "top5 bid[3]");
    CHECK_EQ(t.bids[4].price, 40 * S, "top5 bid[4]");
}

static void test_top5_padding_when_fewer_than_5_levels() {
    auto book = make_book();

    // Only 2 bid levels
    book.update_bid(100 * S, 1 * S);
    book.update_bid(99  * S, 2 * S);

    auto t = book.top5();
    CHECK_EQ(t.bids[0].price, 100 * S, "bid[0] populated");
    CHECK_EQ(t.bids[1].price,  99 * S, "bid[1] populated");
    // Slots 2,3,4 must be zero-padded
    CHECK_EQ(t.bids[2].price, 0LL, "bid[2] zero-padded");
    CHECK_EQ(t.bids[2].qty,   0LL, "bid[2] qty zero-padded");
    CHECK_EQ(t.bids[3].price, 0LL, "bid[3] zero-padded");
    CHECK_EQ(t.bids[4].price, 0LL, "bid[4] zero-padded");
}

static void test_replace_snapshot() {
    auto book = make_book();

    // Pre-populate with some state
    book.update_bid(100 * S, 5 * S);
    book.update_ask(101 * S, 3 * S);
    CHECK_EQ(book.bid_depth(), 1u, "before snapshot: 1 bid");
    CHECK_EQ(book.ask_depth(), 1u, "before snapshot: 1 ask");

    // Snapshot replaces everything
    std::vector<lob::core::Level> snap_bids = {
        {99 * S, 2 * S},
        {98 * S, 4 * S},
    };
    std::vector<lob::core::Level> snap_asks = {
        {102 * S, 1 * S},
        {103 * S, 6 * S},
        {104 * S, 9 * S},
    };
    book.replace_snapshot(snap_bids, snap_asks);

    CHECK_EQ(book.bid_depth(), 2u, "after snapshot: 2 bids");
    CHECK_EQ(book.ask_depth(), 3u, "after snapshot: 3 asks");

    auto t = book.top5();
    CHECK_EQ(t.bids[0].price,  99 * S, "snapshot bid[0]");
    CHECK_EQ(t.bids[1].price,  98 * S, "snapshot bid[1]");
    CHECK_EQ(t.asks[0].price, 102 * S, "snapshot ask[0]");
    CHECK_EQ(t.asks[1].price, 103 * S, "snapshot ask[1]");
    CHECK_EQ(t.asks[2].price, 104 * S, "snapshot ask[2]");

    // The old bid (100) and ask (101) must be gone
    CHECK_EQ(t.bids[2].price, 0LL, "old bid[100] purged");
    CHECK_EQ(t.asks[3].price, 0LL, "old ask[101] purged");
}

static void test_snapshot_ignores_zero_qty_levels() {
    auto book = make_book();

    std::vector<lob::core::Level> bids = {
        {100 * S, 5 * S},
        {99  * S, 0LL},   // qty=0 in snapshot → must NOT insert this level
    };
    book.replace_snapshot(bids, {});

    CHECK_EQ(book.bid_depth(), 1u, "snapshot: zero-qty level not inserted");
    CHECK_EQ(book.top5().bids[0].price, 100 * S, "only non-zero level present");
}

static void test_clear() {
    auto book = make_book();

    book.update_bid(100 * S, 1 * S);
    book.update_ask(101 * S, 1 * S);
    book.clear();

    CHECK_EQ(book.bid_depth(), 0u, "clear: bid depth zero");
    CHECK_EQ(book.ask_depth(), 0u, "clear: ask depth zero");

    auto t = book.top5();
    CHECK_EQ(t.bids[0].price, 0LL, "clear: bids zero");
    CHECK_EQ(t.asks[0].price, 0LL, "clear: asks zero");
}

static void test_crossed_book_allowed() {
    // The book should not enforce bid < ask — that's the exchange's job.
    // We just store what we receive.
    auto book = make_book();
    book.update_bid(105 * S, 1 * S);  // bid higher than ask
    book.update_ask(100 * S, 1 * S);

    auto t = book.top5();
    CHECK_EQ(t.bids[0].price, 105 * S, "crossed: bid stored");
    CHECK_EQ(t.asks[0].price, 100 * S, "crossed: ask stored");
}

int main() {
    std::puts("=== Module 2: Order Book Tests ===\n");

    test_empty_book();
    test_insert_and_update();
    test_qty_zero_removes_level();
    test_bid_ordering_descending();
    test_ask_ordering_ascending();
    test_top5_with_more_than_5_levels();
    test_top5_padding_when_fewer_than_5_levels();
    test_replace_snapshot();
    test_snapshot_ignores_zero_qty_levels();
    test_clear();
    test_crossed_book_allowed();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
