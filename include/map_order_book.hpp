#pragma once

#include "order_book.hpp"
#include <map>
#include <functional>

namespace lob::core {

class MapOrderBook final : public IOrderBook {
public:
    MapOrderBook() = default;

    void update_bid(int64_t price, int64_t qty) override {
        if (qty == 0) {
            bids_.erase(price);
        } else {
            bids_[price] = qty;
        }
    }

    void update_ask(int64_t price, int64_t qty) override {
        if (qty == 0) {
            asks_.erase(price);
        } else {
            asks_[price] = qty;
        }
    }

    void replace_snapshot(const std::vector<Level>& bids,
                          const std::vector<Level>& asks) override {
        bids_.clear();
        for (const auto& l : bids) {
            if (l.qty > 0) bids_[l.price] = l.qty;
        }

        asks_.clear();
        for (const auto& l : asks) {
            if (l.qty > 0) asks_[l.price] = l.qty;
        }
    }

    Top5 top5() const override {
        Top5 result{};

        std::size_t i = 0;
        for (auto it = bids_.begin(); it != bids_.end() && i < 5; ++it, ++i) {
            result.bids[i] = {it->first, it->second};
        }

        i = 0;
        for (auto it = asks_.begin(); it != asks_.end() && i < 5; ++it, ++i) {
            result.asks[i] = {it->first, it->second};
        }

        return result;
    }

    void clear() override {
        bids_.clear();
        asks_.clear();
    }

    std::size_t bid_depth() const override { return bids_.size(); }
    std::size_t ask_depth() const override { return asks_.size(); }

private:
    std::map<int64_t, int64_t, std::greater<int64_t>> bids_;
    std::map<int64_t, int64_t, std::less<int64_t>>    asks_;
};

} // namespace lob::core
