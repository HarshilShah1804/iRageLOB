#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace lob::core {

struct Level {
    int64_t price{0};  // scaled int64: price * 10^8
    int64_t qty{0};    // scaled int64: qty   * 10^8
};

struct Top5 {
    std::array<Level, 5> bids{};
    std::array<Level, 5> asks{};
};

class IOrderBook {
public:
    virtual ~IOrderBook() = default;

    virtual void update_bid(int64_t price, int64_t qty) = 0;

    virtual void update_ask(int64_t price, int64_t qty) = 0;

    virtual void replace_snapshot(const std::vector<Level>& bids,
                                  const std::vector<Level>& asks) = 0;

    virtual Top5 top5() const = 0;

    virtual void clear() = 0;

    virtual std::size_t bid_depth() const = 0;
    virtual std::size_t ask_depth() const = 0;
};

} // namespace lob::core
