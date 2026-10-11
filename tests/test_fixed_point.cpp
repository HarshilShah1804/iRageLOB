#include "fixed_point.hpp"
#include <cassert>
#include <cstdio>
#include <string_view>

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_EQ(actual, expected, label)                               \
    do {                                                                 \
        int64_t _a = (actual);                                           \
        int64_t _e = (expected);                                         \
        if (_a == _e) {                                                  \
            ++g_pass;                                                    \
        } else {                                                         \
            ++g_fail;                                                    \
            std::fprintf(stderr,                                         \
                "[FAIL] %s\n       got      = %lld\n       expected = %lld\n", \
                label, (long long)_a, (long long)_e);                   \
        }                                                                \
    } while (0)

static int64_t fp(std::string_view s) { return lob::core::parse_fixed(s); }


static void test_whole_numbers() {
    EXPECT_EQ(fp("0"),       0LL,              "zero");
    EXPECT_EQ(fp("1"),       100'000'000LL,    "one");
    EXPECT_EQ(fp("100"),     10'000'000'000LL, "100");
    // BTC ballpark: 96420 → 9642000000000
    EXPECT_EQ(fp("96420"),   9'642'000'000'000LL, "96420");
}

static void test_fractional_padding() {
    EXPECT_EQ(fp("96420.5"),     9'642'050'000'000LL,  "96420.5");
    EXPECT_EQ(fp("96420.50"),    9'642'050'000'000LL,  "96420.50");
    EXPECT_EQ(fp("1.12345678"),  112'345'678LL,         "1.12345678");
    EXPECT_EQ(fp("0.1"),         10'000'000LL,          "0.1");
    EXPECT_EQ(fp("0.01"),        1'000'000LL,           "0.01");
}

static void test_full_8_decimal_places() {
    EXPECT_EQ(fp("0.00000001"),  1LL,  "1 tick");
    EXPECT_EQ(fp("0.00012345"),  12'345LL, "0.00012345");
}

static void test_sub_tick_truncation() {
    EXPECT_EQ(fp("0.000000001"),  0LL,  "sub-tick truncation → 0");
    EXPECT_EQ(fp("0.000000019"),  1LL,  "0.000000019 → 1 tick");
    EXPECT_EQ(fp("1.123456789"), 112'345'678LL, "1.123456789 truncated");
}

static void test_sign_handling() {
    EXPECT_EQ(fp("-1"),         -100'000'000LL,  "negative integer");
    EXPECT_EQ(fp("+1"),          100'000'000LL,  "explicit positive");
    EXPECT_EQ(fp("-0.5"),        -50'000'000LL,  "negative fraction");
    EXPECT_EQ(fp("-96420.50"), -9'642'050'000'000LL, "negative BTC price");
}

static void test_edge_cases() {
    EXPECT_EQ(fp(""),           0LL, "empty string");
    EXPECT_EQ(fp("0.0"),        0LL, "0.0");
    EXPECT_EQ(fp("0.00000000"), 0LL, "8 zeros after decimal");
    EXPECT_EQ(fp("007"),        700'000'000LL, "leading zeros");
}

static void test_crypto_realistic_prices() {
    EXPECT_EQ(fp("2534.12"),    253'412'000'000LL, "ETH price 2534.12");
    EXPECT_EQ(fp("178.456789"), 17'845'678'900LL,  "SOL price 178.456789");
    EXPECT_EQ(fp("0.00003456"),     3'456LL,        "tiny alt 0.00003456");
    EXPECT_EQ(fp("15234.56789012"), 1'523'456'789'012LL, "large qty 15234.56789012");
}

int main() {

    test_whole_numbers();
    test_fractional_padding();
    test_full_8_decimal_places();
    test_sub_tick_truncation();
    test_sign_handling();
    test_edge_cases();
    test_crypto_realistic_prices();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
