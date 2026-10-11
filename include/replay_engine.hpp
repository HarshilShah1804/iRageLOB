#pragma once

#include <string>

namespace lob::replay {

bool replay_market_data(const std::string& input_csv_path,
                        const std::string& output_csv_path);

} // namespace lob::replay
