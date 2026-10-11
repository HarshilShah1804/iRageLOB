#pragma once

#include <cstdint>
#include <string_view>

namespace lob::core {

enum class StreamKind : uint8_t {
    DepthDiff,  // differential order book update
    Depth5,     // partial 5-level top-of-book snapshot
    Trade,      // individual trade
};

inline StreamKind stream_kind_from_name(std::string_view name) {
    if (name.find("@depth5") != std::string_view::npos) return StreamKind::Depth5;
    if (name.find("@trade")  != std::string_view::npos) return StreamKind::Trade;
    return StreamKind::DepthDiff;
}

enum class Venue : uint8_t {
    Spot,   // wss://stream.binance.com:9443
    Usdm,   // wss://fstream.binance.com:443
};

// CSV column value for 'venue' field. 
inline std::string_view to_csv_str(Venue v) {
    return (v == Venue::Spot) ? "spot" : "usdm";
}

// CSV column value for 'stream_kind' field. 
inline std::string_view to_csv_str(StreamKind k) {
    switch (k) {
        case StreamKind::DepthDiff: return "depth_diff";
        case StreamKind::Depth5:    return "depth5";
        case StreamKind::Trade:     return "trade";
    }
    return "depth_diff";
}

} // namespace lob::core
