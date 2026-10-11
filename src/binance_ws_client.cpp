#include "binance_ws_client.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <openssl/err.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace basio = boost::asio;
namespace ssl       = boost::asio::ssl;


namespace lob::net {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Build combined stream path:
//   /stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade/...
static std::string make_stream_path(lob::core::Venue venue, const std::vector<std::string>& symbols) {
    std::string prefix = (venue == lob::core::Venue::Spot)
        ? "/stream?streams="
        : "/public/stream?streams=";

    std::string path = prefix;
    bool first = true;
    for (const auto& sym : symbols) {
        const std::string s = to_lower(sym);
        if (!first) path += '/';
        first = false;
        path += s + "@depth@100ms"
             + "/" + s + "@depth5@100ms"
             + "/" + s + "@trade";
    }
    return path;
}

BinanceWsClient::BinanceWsClient(WsConfig cfg, MessageCb cb)
    : cfg_(std::move(cfg)), on_message_(std::move(cb))
{
    if (cfg_.venue == lob::core::Venue::Spot) {
        host_ = "stream.binance.com";
        port_ = "9443";
    } else {
        host_ = "fstream.binance.com";
        port_ = "443";
    }
    stream_path_ = make_stream_path(cfg_.venue, cfg_.symbols);
}

BinanceWsClient::~BinanceWsClient() { stop(); }

void BinanceWsClient::stop() { running_ = false; }

void BinanceWsClient::run() {
    running_ = true;
    const auto start = std::chrono::steady_clock::now();

    while (running_) {
        // Duration guard (checked before each session attempt)
        if (cfg_.duration_seconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= cfg_.duration_seconds) {
                std::cout << "[WS] Duration limit reached. Stopping.\n";
                break;
            }
        }

        bool keep_going = false;
        try {
            keep_going = run_session();
        } catch (const beast::system_error& e) {
            // Treat graceful close as a clean exit, not an error.
            if (e.code() == websocket::error::closed) {
                std::cout << "[WS] Connection closed cleanly.\n";
            } else {
                std::cerr << "[WS] Beast error: " << e.what() << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[WS] Error: " << e.what() << "\n";
        }

        if (!keep_going || !running_) break;

        std::cout << "[WS] Reconnecting in 1 s... (epoch=" << conn_epoch_ + 1 << ")\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// Returns true  → caller should reconnect
// Returns false → caller should exit (stop() called or duration elapsed)
bool BinanceWsClient::run_session() {
    boost::asio::io_context ioc;
    ssl::context    tls{ssl::context::tlsv12_client};
    tls.set_default_verify_paths();
    tls.set_verify_mode(ssl::verify_peer);

    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{ioc, tls};

    boost::asio::ip::tcp::resolver resolver{ioc};
    const auto endpoints = resolver.resolve(host_, port_);

    beast::get_lowest_layer(ws).connect(endpoints);

    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host_.c_str())) {
        throw std::runtime_error("SSL_set_tlsext_host_name failed");
    }

    ws.next_layer().handshake(ssl::stream_base::client);

    ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(boost::beast::http::field::user_agent, "binance-capture/1.0");
    }));

    const std::string host_header = host_ + ":" + port_;
    ws.handshake(host_header, stream_path_);

    ++conn_epoch_;
    conn_seq_ = 0;
    std::cout << "[WS] Connected (epoch=" << conn_epoch_ << ") — "
              << host_ << stream_path_ << "\n";

    beast::flat_buffer buf;
    const auto session_start = std::chrono::steady_clock::now();

    while (running_) {
        // Per-read duration check
        if (cfg_.duration_seconds > 0) {
            // We check total elapsed from run(), but also guard here to avoid
            // blocking on ws.read() past the deadline.
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - session_start).count();
            if (elapsed >= cfg_.duration_seconds) {
                // Graceful close
                beast::error_code ec;
                ws.close(websocket::close_code::normal, ec);
                return false; // don't reconnect
            }
        }

        buf.clear();
        ws.read(buf); // blocks until a complete frame arrives

        ++conn_seq_;

        // buf contains the full text frame (already reassembled by Beast)
        std::string_view msg{
            static_cast<const char*>(buf.data().data()),
            buf.data().size()
        };

        // Binance combined stream envelope:
        //   {"stream":"btcusdt@depth@100ms","data":{...}}
        //
        // We do a minimal string search here rather than a full JSON parse
        // so the hot path stays fast; the callback can parse at its leisure.
        //
        // Find stream name between first `"stream":"` and next `"`.
        static constexpr std::string_view KEY = "\"stream\":\"";
        auto pos = msg.find(KEY);
        std::string_view stream_name;
        std::string_view payload = msg;

        if (pos != std::string_view::npos) {
            pos += KEY.size();
            auto end_pos = msg.find('"', pos);
            if (end_pos != std::string_view::npos) {
                stream_name = msg.substr(pos, end_pos - pos);
            }
        }

        // Extract inner "data" object to bypass full JSON parsing of the envelope
        static constexpr std::string_view DATA_KEY = "\"data\":";
        auto data_pos = msg.find(DATA_KEY);
        if (data_pos != std::string_view::npos) {
            data_pos += DATA_KEY.size();
            auto end_brace = msg.rfind('}');
            if (end_brace != std::string_view::npos && end_brace > data_pos) {
                payload = msg.substr(data_pos, end_brace - data_pos);
            }
        }

        if (on_message_) {
            on_message_(conn_epoch_, conn_seq_, stream_name, payload);
        }
    }

    beast::error_code ec;
    ws.close(websocket::close_code::normal, ec);
    return false;
}

} // namespace lob::net
