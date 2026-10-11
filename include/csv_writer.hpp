#pragma once

#include "types.hpp"
#include "order_book.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace lob::io {

inline constexpr std::string_view MARKET_DATA_HEADER =
    "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json\n";

inline constexpr std::string_view ORDERBOOK_HEADER =
    "tsec,tnsec,seqNo,id,type,side,"
    "bid0,bid1,bid2,bid3,bid4,"
    "bid_size0,bid_size1,bid_size2,bid_size3,bid_size4,"
    "ask0,ask1,ask2,ask3,ask4,"
    "ask_size0,ask_size1,ask_size2,ask_size3,ask_size4\n";

struct MarketDataRow {
    int64_t          recv_tsec{0};
    int32_t          recv_tnsec{0};
    lob::core::Venue            venue{lob::core::Venue::Spot};
    lob::core::StreamKind       kind{lob::core::StreamKind::DepthDiff};
    int32_t          shard_id{0};
    int32_t          conn_epoch{0};
    int64_t          conn_seq{0};
    std::string_view symbol;       // e.g. "BTCUSDT"
    std::string_view payload_json; // inner Binance data object (minified)
};

struct OrderBookRow {
    int64_t  tsec{0};
    int32_t  tnsec{0};
    uint64_t seq_no{0};
    int32_t  id{0};
    char     type_char{'D'}; // 'D', 'S', 'T'
    char     side_char{'N'}; // 'B', 'A', 'N'
    lob::core::Top5     levels{};
};

namespace detail {

template<typename T>
inline void append_int(std::string& out, T val) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    out.append(buf, ptr);
}

inline void append_quoted(std::string& out, std::string_view val) {
    out.push_back('"');
    for (char c : val) {
        if (c == '"') out.push_back('"');  // RFC 4180: "" escapes a literal "
        out.push_back(c);
    }
    out.push_back('"');
}

} // namespace detail

inline void format_market_data_row(const MarketDataRow& r, std::string& out) {
    using namespace detail;
    append_int(out, r.recv_tsec);  out.push_back(',');
    append_int(out, r.recv_tnsec); out.push_back(',');
    out.append(lob::core::to_csv_str(r.venue));    out.push_back(',');
    out.append(lob::core::to_csv_str(r.kind));     out.push_back(',');
    append_int(out, r.shard_id);   out.push_back(',');
    append_int(out, r.conn_epoch); out.push_back(',');
    append_int(out, r.conn_seq);   out.push_back(',');
    out.append(r.symbol);               out.push_back(',');
    append_quoted(out, r.payload_json);
    out.push_back('\n');
}

inline void format_orderbook_row(const OrderBookRow& r, std::string& out) {
    using namespace detail;

    append_int(out, r.tsec);    out.push_back(',');
    append_int(out, r.tnsec);   out.push_back(',');
    append_int(out, r.seq_no);  out.push_back(',');
    append_int(out, r.id);      out.push_back(',');
    out.push_back(r.type_char); out.push_back(',');
    out.push_back(r.side_char); out.push_back(',');

    // 5 bid prices
    for (int i = 0; i < 5; ++i) { append_int(out, r.levels.bids[i].price); out.push_back(','); }
    // 5 bid sizes
    for (int i = 0; i < 5; ++i) { append_int(out, r.levels.bids[i].qty);   out.push_back(','); }
    // 5 ask prices
    for (int i = 0; i < 5; ++i) { append_int(out, r.levels.asks[i].price); out.push_back(','); }
    // 5 ask sizes
    for (int i = 0; i < 4; ++i) { append_int(out, r.levels.asks[i].qty);   out.push_back(','); }
    append_int(out, r.levels.asks[4].qty);

    out.push_back('\n');
}

} // namespace lob::io
