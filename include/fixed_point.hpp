#pragma once

#include <cstdint>
#include <string_view>

namespace lob::core {

constexpr int64_t FIXED_SCALE = 100'000'000LL;

inline int64_t parse_fixed(std::string_view str) {
    if (str.empty()) return 0;

    const char* p   = str.data();
    const char* end = p + str.size();

    // Optional Negative Sign
    bool negative = false;
    if      (*p == '-') { negative = true; ++p; }
    else if (*p == '+') {                  ++p; }

    int64_t integer_part = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        integer_part = integer_part * 10 + (*p - '0');
        ++p;
    }

    int64_t frac_part   = 0;
    int     frac_digits = 0;

    if (p < end && *p == '.') {
        ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            if (frac_digits < 8) {
                frac_part = frac_part * 10 + (*p - '0');
                ++frac_digits;
            }
            ++p;
        }
    }

    while (frac_digits < 8) {
        frac_part *= 10;
        ++frac_digits;
    }

    int64_t result = integer_part * FIXED_SCALE + frac_part;
    return negative ? -result : result;
}

} // namespace lob::core
