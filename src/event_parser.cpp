#include "event_parser.hpp"

namespace lob::parser {

ParseResult EventParser::parse(lob::core::StreamKind kind,
                               std::string_view inner_json,
                               lob::core::IOrderBook& book) {
    if (inner_json.empty()) return {};

    // Parse into the reusable document (clears previous state automatically)
    doc_.Parse(inner_json.data(), inner_json.size());

    if (doc_.HasParseError() || !doc_.IsObject()) return {};

    switch (kind) {
        case lob::core::StreamKind::DepthDiff: return parse_depth_diff(book);
        case lob::core::StreamKind::Depth5:    return parse_depth5(book);
        case lob::core::StreamKind::Trade:     return parse_trade();
    }
    return {};
}


ParseResult EventParser::parse_depth_diff(lob::core::IOrderBook& book) {
    // Live payload keys:
    //   "b" → bid diffs  [ ["price_str", "qty_str"], ... ]
    //   "a" → ask diffs  [ ["price_str", "qty_str"], ... ]
    // qty_str == "0.00000000" means remove that level.

    bool had_bids = false;
    bool had_asks = false;

    if (doc_.HasMember("b") && doc_["b"].IsArray()) {
        const auto& arr = doc_["b"];
        for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
            const auto& entry = arr[i];
            if (!entry.IsArray() || entry.Size() < 2) continue;
            if (!entry[0].IsString() || !entry[1].IsString()) continue;

            int64_t price = lob::core::parse_fixed(entry[0].GetString());
            int64_t qty   = lob::core::parse_fixed(entry[1].GetString());
            book.update_bid(price, qty);
            had_bids = true;
        }
    }

    if (doc_.HasMember("a") && doc_["a"].IsArray()) {
        const auto& arr = doc_["a"];
        for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
            const auto& entry = arr[i];
            if (!entry.IsArray() || entry.Size() < 2) continue;
            if (!entry[0].IsString() || !entry[1].IsString()) continue;

            int64_t price = lob::core::parse_fixed(entry[0].GetString());
            int64_t qty   = lob::core::parse_fixed(entry[1].GetString());
            book.update_ask(price, qty);
            had_asks = true;
        }
    }

    // Determine which side was touched for the CSV 'side' column
    char side = 'N';
    if      ( had_bids && !had_asks) side = 'B';
    else if (!had_bids &&  had_asks) side = 'S';
    // else both or neither → 'N'

    return ParseResult{/*ok=*/true, /*type=*/'D', /*side=*/side};
}

ParseResult EventParser::parse_depth5(lob::core::IOrderBook& book) {
    // Live payload keys:
    //   "bids" → top-5 bid levels  [ ["price_str", "qty_str"], ... ]
    //   "asks" → top-5 ask levels  [ ["price_str", "qty_str"], ... ]
    // This is a full replacement of top-of-book state.

    std::vector<lob::core::Level> bids;
    std::vector<lob::core::Level> asks;
    bids.reserve(5);
    asks.reserve(5);

    if (doc_.HasMember("bids") && doc_["bids"].IsArray()) {
        const auto& arr = doc_["bids"];
        for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
            const auto& entry = arr[i];
            if (!entry.IsArray() || entry.Size() < 2) continue;
            if (!entry[0].IsString() || !entry[1].IsString()) continue;
            bids.push_back({lob::core::parse_fixed(entry[0].GetString()),
                            lob::core::parse_fixed(entry[1].GetString())});
        }
    }

    if (doc_.HasMember("asks") && doc_["asks"].IsArray()) {
        const auto& arr = doc_["asks"];
        for (rapidjson::SizeType i = 0; i < arr.Size(); ++i) {
            const auto& entry = arr[i];
            if (!entry.IsArray() || entry.Size() < 2) continue;
            if (!entry[0].IsString() || !entry[1].IsString()) continue;
            asks.push_back({lob::core::parse_fixed(entry[0].GetString()),
                            lob::core::parse_fixed(entry[1].GetString())});
        }
    }

    book.replace_snapshot(bids, asks);
    return ParseResult{/*ok=*/true, /*type=*/'S', /*side=*/'N'};
}

ParseResult EventParser::parse_trade() {
    // Live payload:
    //   "p"  → price string
    //   "q"  → qty string
    //   "m"  → bool; true = buyer is market maker (seller is taker = sell trade)
    //                false = buyer is taker (buy trade)
    //
    // Trades do NOT modify the order book levels — they annotate the current
    // top-5 state. The caller decides whether to emit a snapshot row for trades.

    ParseResult r;
    r.ok        = false;
    r.type_char = 'T';

    if (!doc_.HasMember("p") || !doc_["p"].IsString()) return r;
    if (!doc_.HasMember("q") || !doc_["q"].IsString()) return r;

    r.trade_price    = lob::core::parse_fixed(doc_["p"].GetString());
    r.trade_qty      = lob::core::parse_fixed(doc_["q"].GetString());
    r.buyer_is_maker = doc_.HasMember("m") && doc_["m"].IsBool() && doc_["m"].GetBool();

    // side: 'B' = buy (taker bought), 'S' = ask/sell (taker sold)
    r.side_char = r.buyer_is_maker ? 'S' : 'B';
    r.ok        = true;
    return r;
}

} // namespace lob::parser
