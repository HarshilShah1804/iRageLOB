#pragma once

#include "types.hpp"   // lob::core::Venue, StreamKind
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <cstdint>
#include <string_view>

namespace lob::net {


struct WsConfig {
    lob::core::Venue venue{lob::core::Venue::Spot};
    std::vector<std::string> symbols{"BTCUSDT"};  // uppercase
    int duration_seconds{0};                       // 0 = run until SIGINT
};

class BinanceWsClient {
public:
    using MessageCb = std::function<void(int32_t        conn_epoch,
                                         int64_t        conn_seq,
                                         std::string_view stream_name,
                                         std::string_view raw_payload)>;

    explicit BinanceWsClient(WsConfig cfg, MessageCb cb);
    ~BinanceWsClient();

    // Blocking: connects, reads, reconnects until stop() is called or duration elapses.
    void run();
    void stop();

private:
    // Returns false when duration elapsed or stop() was called.
    bool run_session();

    WsConfig   cfg_;
    MessageCb  on_message_;
    std::atomic<bool> running_{false};
    int32_t conn_epoch_{0};
    int64_t conn_seq_{0};

    // Derived at construction time
    std::string host_;
    std::string port_;
    std::string stream_path_;  // e.g. /stream?streams=btcusdt@depth@100ms/...
};

} // namespace lob::net
