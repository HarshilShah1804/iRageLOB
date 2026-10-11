#include "csv_writer.hpp"
#include "map_order_book.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <sstream>

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
        auto _a = (a); auto _b = (b);                                       \
        if (_a == _b) { ++g_pass; }                                         \
        else { ++g_fail; std::fprintf(stderr,                               \
            "[FAIL] %s  got=\"%s\" expected=\"%s\"  (line %d)\n",           \
            label, std::to_string((long long)_a).c_str(),                   \
                   std::to_string((long long)_b).c_str(), __LINE__); }      \
    } while (0)

/** Count the number of comma-separated fields in a CSV row (ignoring quoted commas). */
static int count_columns(const std::string& line) {
    int cols = 1;
    bool in_quotes = false;
    for (char c : line) {
        if (c == '"')  { in_quotes = !in_quotes; continue; }
        if (c == '\n') break;
        if (c == ',' && !in_quotes) ++cols;
    }
    return cols;
}

/** Extract the Nth field (0-indexed) from a CSV row, unquoted. */
static std::string get_field(const std::string& line, int target_col) {
    int col = 0;
    bool in_quotes = false;
    std::string field;

    for (size_t i = 0; i < line.size() && line[i] != '\n'; ++i) {
        char c = line[i];
        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                // Escaped quote inside quoted field
                field.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
            continue;
        }
        if (c == ',' && !in_quotes) {
            if (col == target_col) return field;
            ++col;
            field.clear();
            continue;
        }
        if (col == target_col || target_col < 0) field.push_back(c);
    }
    return (col == target_col) ? field : "";
}

static constexpr int64_t S = 100'000'000LL;


static void test_market_data_column_count() {
    lob::io::MarketDataRow r;
    r.recv_tsec    = 1728403200;
    r.recv_tnsec   = 123456789;
    r.venue        = lob::core::Venue::Spot;
    r.kind         = lob::core::StreamKind::DepthDiff;
    r.shard_id     = 0;
    r.conn_epoch   = 1;
    r.conn_seq     = 42;
    r.symbol       = "BTCUSDT";
    r.payload_json = R"({"e":"depthUpdate","s":"BTCUSDT"})";

    std::string out;
    lob::io::format_market_data_row(r, out);

    CHECK_EQ(count_columns(out), 9, "market_data: exactly 9 columns");
}

static void test_market_data_field_values() {
    lob::io::MarketDataRow r;
    r.recv_tsec    = 1000000001LL;
    r.recv_tnsec   = 999999999;
    r.venue        = lob::core::Venue::Usdm;
    r.kind         = lob::core::StreamKind::Trade;
    r.shard_id     = 0;
    r.conn_epoch   = 3;
    r.conn_seq     = 999;
    r.symbol       = "ETHUSDT";
    r.payload_json = R"({"e":"trade","p":"2500.00"})";

    std::string out;
    lob::io::format_market_data_row(r, out);

    CHECK(get_field(out, 0) == "1000000001",   "md: recv_tsec");
    CHECK(get_field(out, 1) == "999999999",    "md: recv_tnsec");
    CHECK(get_field(out, 2) == "usdm",         "md: venue=usdm");
    CHECK(get_field(out, 3) == "trade",        "md: stream_kind=trade");
    CHECK(get_field(out, 4) == "0",            "md: shard_id");
    CHECK(get_field(out, 5) == "3",            "md: conn_epoch");
    CHECK(get_field(out, 6) == "999",          "md: conn_seq");
    CHECK(get_field(out, 7) == "ETHUSDT",      "md: symbol");
    // get_field unquotes the payload_json column
    CHECK(get_field(out, 8) == R"({"e":"trade","p":"2500.00"})", "md: payload_json unquoted");
}

static void test_stream_kind_csv_strings() {
    auto make_row = [](lob::core::StreamKind k) {
        lob::io::MarketDataRow r;
        r.payload_json = "{}";
        r.kind = k;
        std::string out;
        lob::io::format_market_data_row(r, out);
        return get_field(out, 3);
    };
    CHECK(make_row(lob::core::StreamKind::DepthDiff) == "depth_diff", "kind: depth_diff");
    CHECK(make_row(lob::core::StreamKind::Depth5)    == "depth5",     "kind: depth5");
    CHECK(make_row(lob::core::StreamKind::Trade)     == "trade",      "kind: trade");
}

static void test_market_data_rfc4180_escaping() {
    // payload_json contains double-quotes — they must be doubled inside the quoted field
    lob::io::MarketDataRow r;
    r.payload_json = R"({"key":"value","n":42})";
    std::string out;
    lob::io::format_market_data_row(r, out);

    // The raw row must start with " and contain "" for each internal "
    // Field 8 (payload_json) when unescaped must recover the original string
    CHECK(get_field(out, 8) == R"({"key":"value","n":42})", "rfc4180: round-trip correct");

    // Verify the raw bytes actually contain doubled quotes
    // e.g. the column should appear as: "{"key":"value","n":42}"  →  ""{""key"":""value"","n"":42}""
    // i.e. raw form has "" for every "
    // Let's check the raw field manually
    size_t comma7 = std::string::npos;
    int commas = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == ',' && ++commas == 8) { comma7 = i; break; }
    }
    CHECK(comma7 != std::string::npos, "rfc4180: found 8th comma");
    if (comma7 != std::string::npos) {
        // raw field starts with "
        CHECK(out[comma7 + 1] == '"', "rfc4180: payload_json field is quoted");
        // Must contain "" somewhere (the escaped internal quotes)
        std::string raw_field = out.substr(comma7 + 1);
        CHECK(raw_field.find("\"\"") != std::string::npos, "rfc4180: internal quotes doubled");
    }
}

static void test_market_data_ends_with_newline() {
    lob::io::MarketDataRow r;
    r.payload_json = "{}";
    std::string out;
    lob::io::format_market_data_row(r, out);
    CHECK(!out.empty() && out.back() == '\n', "md: row ends with newline");
}

static void test_orderbook_column_count() {
    // The single most critical check — reviewer runs:
    //   awk -F',' 'NR==2{print NF}' *_orderbook.csv
    // and expects exactly 26.
    lob::io::OrderBookRow r;
    r.tsec      = 1728403200;
    r.tnsec     = 0;
    r.seq_no    = 1;
    r.id        = 1;
    r.type_char = 'D';
    r.side_char = 'N';
    // populate some levels
    r.levels.bids[0] = {100 * S, 5 * S};
    r.levels.bids[1] = { 99 * S, 3 * S};
    r.levels.asks[0] = {101 * S, 2 * S};
    r.levels.asks[1] = {102 * S, 1 * S};
    // slots 2,3,4 remain zero

    std::string out;
    lob::io::format_orderbook_row(r, out);

    CHECK_EQ(count_columns(out), 26, "orderbook: exactly 26 columns");
}

static void test_orderbook_field_positions() {
    lob::io::OrderBookRow r;
    r.tsec      = 1728403200LL;
    r.tnsec     = 500000000;
    r.seq_no    = 7;
    r.id        = 2;
    r.type_char = 'S';
    r.side_char = 'B';
    r.levels.bids[0] = {82633'95'000'000LL, 697'443'000LL};
    r.levels.asks[0] = {82634'00'000'000LL, 123'000'000LL};

    std::string out;
    lob::io::format_orderbook_row(r, out);

    // Check fixed column positions
    CHECK(get_field(out,  0) == "1728403200",     "ob: tsec col 0");
    CHECK(get_field(out,  1) == "500000000",      "ob: tnsec col 1");
    CHECK(get_field(out,  2) == "7",              "ob: seqNo col 2");
    CHECK(get_field(out,  3) == "2",              "ob: id col 3");
    CHECK(get_field(out,  4) == "S",              "ob: type col 4");
    CHECK(get_field(out,  5) == "B",              "ob: side col 5");
    // col 6 = bid0 price
    CHECK(get_field(out,  6) == "8263395000000",  "ob: bid0 price col 6");
    // col 11 = bid_size0
    CHECK(get_field(out, 11) == "697443000",      "ob: bid_size0 col 11");
    // col 16 = ask0 price
    CHECK(get_field(out, 16) == "8263400000000",  "ob: ask0 price col 16");
    // col 21 = ask_size0
    CHECK(get_field(out, 21) == "123000000",      "ob: ask_size0 col 21");
}

static void test_orderbook_zero_padding() {
    // Empty book → all price and size slots must be "0"
    lob::io::OrderBookRow r;
    r.type_char = 'D';
    r.side_char = 'N';
    // levels default-initialised to {0,0}

    std::string out;
    lob::io::format_orderbook_row(r, out);

    CHECK_EQ(count_columns(out), 26, "zero-pad: still 26 columns");
    for (int col = 6; col < 26; ++col) {
        CHECK(get_field(out, col) == "0", "zero-pad: all level slots are 0");
    }
}

static void test_orderbook_type_chars() {
    auto make_row = [](char type, char side) {
        lob::io::OrderBookRow r;
        r.type_char = type;
        r.side_char = side;
        std::string out;
        lob::io::format_orderbook_row(r, out);
        return std::make_pair(get_field(out, 4), get_field(out, 5));
    };

    auto [t, s] = make_row('D', 'N'); CHECK(t=="D" && s=="N", "type D side N");
    auto [t2,s2]= make_row('S', 'N'); CHECK(t2=="S"&& s2=="N","type S side N");
    auto [t3,s3]= make_row('T', 'B'); CHECK(t3=="T"&& s3=="B","type T side B");
    auto [t4,s4]= make_row('T', 'S'); CHECK(t4=="T"&& s4=="S","type T side S");
}

static void test_orderbook_ends_with_newline() {
    lob::io::OrderBookRow r;
    std::string out;
    lob::io::format_orderbook_row(r, out);
    CHECK(!out.empty() && out.back() == '\n', "ob: row ends with newline");
}

static void test_headers_format() {
    // Headers must end with \n and not have trailing comma
    const auto& mdh = lob::io::MARKET_DATA_HEADER;
    CHECK(mdh.back() == '\n', "md header: ends with newline");
    CHECK(count_columns(std::string(mdh)) == 9, "md header: 9 columns");

    const auto& obh = lob::io::ORDERBOOK_HEADER;
    CHECK(obh.back() == '\n', "ob header: ends with newline");
    CHECK(count_columns(std::string(obh)) == 26, "ob header: 26 columns");
}

static void test_accumulate_multiple_rows() {
    // Ensure format functions append to (not replace) `out`
    std::string out;
    lob::io::OrderBookRow r1, r2;
    r1.seq_no = 1; r1.type_char = 'D'; r1.side_char = 'N';
    r2.seq_no = 2; r2.type_char = 'S'; r2.side_char = 'N';

    lob::io::format_orderbook_row(r1, out);
    lob::io::format_orderbook_row(r2, out);

    // Split on newline and check both rows present
    size_t nl = out.find('\n');
    CHECK(nl != std::string::npos, "multi-row: first newline found");
    std::string row1 = out.substr(0, nl);
    std::string row2 = out.substr(nl + 1);
    CHECK(get_field(row1, 2) == "1", "multi-row: row1 seqNo=1");
    CHECK(get_field(row2, 2) == "2", "multi-row: row2 seqNo=2");
}

int main() {
    std::puts("=== Module 4: CSV Writer Tests ===\n");

    // Deliverable A
    test_market_data_column_count();
    test_market_data_field_values();
    test_stream_kind_csv_strings();
    test_market_data_rfc4180_escaping();
    test_market_data_ends_with_newline();

    // Deliverable B
    test_orderbook_column_count();
    test_orderbook_field_positions();
    test_orderbook_zero_padding();
    test_orderbook_type_chars();
    test_orderbook_ends_with_newline();
    test_headers_format();
    test_accumulate_multiple_rows();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
