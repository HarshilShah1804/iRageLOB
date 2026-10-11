#include "replay_engine.hpp"
#include "event_parser.hpp"
#include "map_order_book.hpp"
#include "csv_writer.hpp"
#include "types.hpp"

#include <fstream>
#include <charconv>

namespace lob::replay {


static void unescape_rfc4180(std::string_view in, std::string& out) {
    out.clear();
    if (in.size() >= 2 && in.front() == '"' && in.back() == '"') {
        in.remove_prefix(1);
        in.remove_suffix(1);
    }
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '"' && i + 1 < in.size() && in[i + 1] == '"') {
            out.push_back('"');
            ++i; // skip the second quote
        } else {
            out.push_back(in[i]);
        }
    }
}

static std::string_view next_csv_field(std::string_view& row) {
    size_t pos = row.find(',');
    if (pos == std::string_view::npos) {
        auto ret = row;
        row = {};
        return ret;
    }
    auto ret = row.substr(0, pos);
    row.remove_prefix(pos + 1);
    return ret;
}

bool replay_market_data(const std::string& input_csv_path,
                        const std::string& output_csv_path) {
    std::ifstream in(input_csv_path);
    if (!in) return false;

    std::ofstream out(output_csv_path);
    if (!out) return false;

    out << lob::io::ORDERBOOK_HEADER;

    std::string line;
    // Skip the Deliverable A header
    if (std::getline(in, line)) {
        if (line.empty() || line[0] != 'r') {
            // "recv_tsec..." header not found, assume it's data
            in.seekg(0);
        }
    }

    lob::core::MapOrderBook book;
    lob::parser::EventParser  parser;
    std::string  unescaped_json;
    std::string  out_buffer;
    out_buffer.reserve(4096);

    int32_t row_id = 1;

    // Columns:
    // 0: recv_tsec (int64)
    // 1: recv_tnsec (int32)
    // 2: venue
    // 3: stream_kind (mapped to lob::core::StreamKind)
    // 4: shard_id
    // 5: conn_epoch
    // 6: conn_seq (uint64)
    // 7: symbol
    // 8: payload_json (RFC 4180 quoted)

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back(); // handle Windows CRLF gracefully

        std::string_view row = line;

        auto f_tsec   = next_csv_field(row);
        auto f_tnsec  = next_csv_field(row);
        next_csv_field(row); // skip venue
        auto f_kind   = next_csv_field(row);
        next_csv_field(row); // skip shard_id
        next_csv_field(row); // skip conn_epoch
        auto f_seq    = next_csv_field(row);
        next_csv_field(row); // skip symbol
        auto f_json   = row; // everything remaining is the JSON column

        int64_t  tsec = 0;
        int32_t  tnsec = 0;
        uint64_t seq = 0;
        std::from_chars(f_tsec.data(), f_tsec.data() + f_tsec.size(), tsec);
        std::from_chars(f_tnsec.data(), f_tnsec.data() + f_tnsec.size(), tnsec);
        std::from_chars(f_seq.data(), f_seq.data() + f_seq.size(), seq);

        lob::core::StreamKind kind = lob::core::StreamKind::DepthDiff;
        if      (f_kind == "depth5") kind = lob::core::StreamKind::Depth5;
        else if (f_kind == "trade")  kind = lob::core::StreamKind::Trade;

        unescape_rfc4180(f_json, unescaped_json);

        // Apply update to orderbook
        lob::parser::ParseResult pr = parser.parse(kind, unescaped_json, book);
        if (!pr.ok) continue;

        // Build and write the resulting row
        lob::io::OrderBookRow ob_row;
        ob_row.tsec      = tsec;
        ob_row.tnsec     = tnsec;
        ob_row.seq_no    = seq;
        ob_row.id        = row_id++;
        ob_row.type_char = pr.type_char;
        ob_row.side_char = pr.side_char;
        ob_row.levels    = book.top5();

        out_buffer.clear();
        lob::io::format_orderbook_row(ob_row, out_buffer);
        out << out_buffer;
    }

    return true;
}

} // namespace lob::replay
