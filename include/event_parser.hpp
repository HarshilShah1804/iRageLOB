#pragma once

#include "types.hpp"
#include "order_book.hpp"
#include "fixed_point.hpp"

#include <rapidjson/document.h>
#include <string_view>
#include <cstdint>

namespace lob::parser {

struct ParseResult {
    bool       ok{false};

    // Maps directly to the orderbook CSV columns 'type' and 'side'
    // type: 'D' = depthUpdate, 'S' = depth5 snapshot, 'T' = trade
    // side: 'B' = bid-side, 'S' = ask-side, 'N' = symmetric / N/A
    char type_char{'D'};
    char side_char{'N'};

    // Trade metadata (only populated when kind == Trade)
    int64_t trade_price{0};
    int64_t trade_qty{0};
    bool    buyer_is_maker{false};
};

class EventParser {
public:
    EventParser() = default;

    ParseResult parse(lob::core::StreamKind kind, std::string_view inner_json, lob::core::IOrderBook& book);

private:
    ParseResult parse_depth_diff(lob::core::IOrderBook& book);
    ParseResult parse_depth5    (lob::core::IOrderBook& book);
    ParseResult parse_trade     ();

    rapidjson::Document doc_;   // reused across calls to avoid re-allocation
};

} // namespace lob::parser
