#include "binance_ws_client.hpp"
#include "event_parser.hpp"
#include "map_order_book.hpp"
#include "csv_writer.hpp"
#include "replay_engine.hpp"
#include "types.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <ctime>

static std::atomic<bool> g_stop{false};
static lob::net::BinanceWsClient* g_client{nullptr};

static void on_signal(int) {
    std::cout << "\n[Main] Signal received — shutting down gracefully...\n";
    g_stop = true;
    if (g_client) g_client->stop();
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) {
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

static void usage(const char* prog) {
    std::cout
        << "Usage:\n"
        << "  Live Capture:\n"
        << "    " << prog << " [--live] [options]\n"
        << "      --venue spot|usdm          (default: spot)\n"
        << "      --symbols SYM1,SYM2,...    (default: BTCUSDT)\n"
        << "      --duration <seconds>       (default: 0 = run until Ctrl-C)\n"
        << "      --shard <id>               (default: 0)\n"
        << "      --output-dir <path>        (default: .)\n"
        << "\n  Offline Replay:\n"
        << "    " << prog << " --replay <input_market_data.csv> <output_orderbook.csv>\n"
        << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    std::string first_arg = argv[1];

    if (first_arg == "-h" || first_arg == "--help") {
        usage(argv[0]);
        return 0;
    }

    if (first_arg == "--replay") {
        if (argc < 4) { usage(argv[0]); return 1; }
        std::string in_csv  = argv[2];
        std::string out_csv = argv[3];

        std::cout << "[Replay] Starting offline parity generation...\n";
        std::cout << "[Replay] Input:  " << in_csv << "\n";
        std::cout << "[Replay] Output: " << out_csv << "\n";

        bool ok = lob::replay::replay_market_data(in_csv, out_csv);
        if (ok) {
            std::cout << "[Replay] Success. Data mirrored deterministically.\n";
            return 0;
        } else {
            std::cerr << "[Replay] Fatal: Failed to process files.\n";
            return 1;
        }
    }

    lob::net::WsConfig cfg;
    int32_t shard_id = 0;
    std::string output_dir = ".";

    int start_idx = 1;
    if (first_arg == "--live") {
        start_idx = 2;
    }

    for (int i = start_idx; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--venue" && i + 1 < argc) {
            cfg.venue = (std::string(argv[++i]) == "usdm") ? lob::core::Venue::Usdm : lob::core::Venue::Spot;
        } else if (a == "--symbols" && i + 1 < argc) {
            cfg.symbols = split(argv[++i], ',');
        } else if (a == "--duration" && i + 1 < argc) {
            cfg.duration_seconds = std::stoi(argv[++i]);
        } else if (a == "--shard" && i + 1 < argc) {
            shard_id = std::stoi(argv[++i]);
        } else if (a == "--output-dir" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (a == "--live") {
            // Optional flag
        } else if (a == "--help" || a == "-h") {
            usage(argv[0]); return 0;
        } else {
            std::cerr << "Unknown arg: " << a << "\n";
            usage(argv[0]); return 1;
        }
    }

    std::error_code ec;
    if (!output_dir.empty() && output_dir != ".") {
        std::filesystem::create_directories(output_dir, ec);
        if (ec) {
            std::cerr << "[Live] Fatal: Failed to create output directory: " << ec.message() << "\n";
            return 1;
        }
    }

    std::time_t now_c = std::time(nullptr);
    std::tm tm_buf{};
    gmtime_r(&now_c, &tm_buf);
    char date_buf[32];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_buf);

    std::string venue_str = (cfg.venue == lob::core::Venue::Spot ? "spot" : "usdm");
    std::string sym_str = (cfg.symbols.size() == 1 ? cfg.symbols[0] : "multi");
    std::transform(sym_str.begin(), sym_str.end(), sym_str.begin(), ::toupper);

    std::string base_name = "market_data_" + venue_str + "_" + sym_str + "_" + date_buf;
    std::string md_filename = base_name + ".csv";
    std::string ob_filename = base_name + "_orderbook.csv";

    std::filesystem::path md_path = std::filesystem::path(output_dir) / md_filename;
    std::filesystem::path ob_path = std::filesystem::path(output_dir) / ob_filename;

    std::ofstream md_out(md_path);
    std::ofstream ob_out(ob_path);
    if (!md_out || !ob_out) {
        std::cerr << "[Live] Fatal: Failed to open output CSVs at: " << md_path << ", " << ob_path << "\n";
        return 1;
    }
    md_out << lob::io::MARKET_DATA_HEADER;
    ob_out << lob::io::ORDERBOOK_HEADER;

    // Create convenient root symlinks if outputting to current directory
    if (output_dir == "." || output_dir.empty()) {
        std::filesystem::remove("market_data.csv", ec);
        std::filesystem::create_symlink(md_filename, "market_data.csv", ec);
        std::filesystem::remove("orderbook.csv", ec);
        std::filesystem::create_symlink(ob_filename, "orderbook.csv", ec);
    }

        std::unordered_map<std::string, lob::core::MapOrderBook> books;
        lob::parser::EventParser  parser;
        int32_t current_id = 1;
        
        std::string md_buf;
        std::string ob_buf;
        md_buf.reserve(4096);
        ob_buf.reserve(4096);

        auto on_msg = [&](int32_t epoch, int64_t seq, std::string_view stream, std::string_view payload) {
            // High-resolution timestamp
            auto now  = std::chrono::system_clock::now().time_since_epoch();
            auto sec  = std::chrono::duration_cast<std::chrono::seconds>(now).count();
            auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() % 1000000000;

            lob::core::StreamKind kind = lob::core::stream_kind_from_name(stream);

            size_t at_pos = stream.find('@');
            std::string sym = (at_pos != std::string_view::npos) 
                ? std::string(stream.substr(0, at_pos)) : "UNKNOWN";
            std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);

            // 1. Deliverable A: Market Data (Audit Log)
            lob::io::MarketDataRow mr;
            mr.recv_tsec    = sec;
            mr.recv_tnsec   = nsec;
            mr.venue        = cfg.venue;
            mr.kind         = kind;
            mr.shard_id     = shard_id;
            mr.conn_epoch   = epoch;
            mr.conn_seq     = seq;
            mr.symbol       = sym;
            mr.payload_json = payload;

            md_buf.clear();
            lob::io::format_market_data_row(mr, md_buf);
            md_out << md_buf;

            // 2. Process Order Book
            lob::parser::ParseResult pr = parser.parse(kind, payload, books[sym]);
            if (pr.ok) {
                // 3. Deliverable B: Order Book State
                lob::io::OrderBookRow orow;
                orow.tsec      = sec;
                orow.tnsec     = nsec;
                orow.seq_no    = seq;
                orow.id        = current_id++;
                orow.type_char = pr.type_char;
                orow.side_char = pr.side_char;
                orow.levels    = books[sym].top5();

                ob_buf.clear();
                lob::io::format_orderbook_row(orow, ob_buf);
                ob_out << ob_buf;
            }
        };

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    std::cout << "[Live] Starting capture...\n"
              << "  Venue:         " << venue_str << "\n"
              << "  Symbols:       " << (cfg.symbols.empty() ? "None" : cfg.symbols[0]) << " ...\n"
              << "  Duration:      " << (cfg.duration_seconds > 0 ? std::to_string(cfg.duration_seconds) + "s" : "Infinite (Ctrl-C to stop)") << "\n"
              << "  Deliverable A: " << md_path.string() << "\n"
              << "  Deliverable B: " << ob_path.string() << "\n\n";

    try {
        lob::net::BinanceWsClient client(cfg, on_msg);
        g_client = &client;
        client.run();
        g_client = nullptr;
    } catch (const std::exception& e) {
        std::cerr << "[Main] Fatal: " << e.what() << "\n";
        return 1;
    }

    std::cout << "[Live] Shutdown complete. Data flushed to disk.\n";
    return 0;
}
