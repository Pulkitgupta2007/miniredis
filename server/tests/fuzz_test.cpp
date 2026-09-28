#include <gtest/gtest.h>
#include "miniredis/resp.h"
#include "miniredis/command.h"
#include "miniredis/store.h"

#include <random>
#include <string>
#include <vector>

using namespace miniredis;

class RespParserFuzzTest : public ::testing::Test {
protected:
    void SetUp() override {
        rng_.seed(42);
    }

    std::string generate_random_bytes(size_t len) {
        std::uniform_int_distribution<int> dist(0, 255);
        std::string s;
        s.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            s.push_back(static_cast<char>(dist(rng_)));
        }
        return s;
    }

    std::mt19937 rng_;
};

// 1. Fuzz with pure random byte streams of varying lengths
TEST_F(RespParserFuzzTest, PureRandomByteStreamsNeverCrash) {
    constexpr int kIterations = 10000;
    std::uniform_int_distribution<size_t> len_dist(0, 4096);

    for (int i = 0; i < kIterations; ++i) {
        size_t len = len_dist(rng_);
        std::string payload = generate_random_bytes(len);

        // Fuzz both parse() and parse_command()
        ParseResult r1 = RespParser::parse(payload);
        (void)r1;

        ParseResult r2 = RespParser::parse_command(payload);
        (void)r2;
    }
}

// 2. Fuzz with extreme and boundary numbers in length prefixes
TEST_F(RespParserFuzzTest, ExtremeNumericLengthPrefixes) {
    const std::vector<std::string> malformed_headers = {
        "$9223372036854775807\r\nfoo\r\n",       // INT64_MAX
        "$-9223372036854775808\r\nfoo\r\n",      // INT64_MIN
        "$18446744073709551615\r\nfoo\r\n",      // UINT64_MAX
        "$999999999999999999999999999999\r\n",  // BigInt overflow
        "$-2\r\n",                               // Invalid negative len
        "$-100\r\n",
        "$0\r\n\r\n",                            // Empty bulk string
        "$0\r\n",                                // Truncated empty bulk string
        "*9223372036854775807\r\n",              // Huge array count
        "*1000000000\r\n",                       // 1B array count
        "*-2\r\n",                               // Invalid negative array count
        ":9223372036854775807\r\n",              // Max integer
        ":-9223372036854775808\r\n",             // Min integer
        ":999999999999999999999999999999\r\n",  // Integer overflow
        ":--5\r\n",
        ":++5\r\n",
        ":abc\r\n",
        "+",
        "-",
        ":",
        "$",
        "*",
        "+\r\n",
        "-\r\n",
        ":\r\n",
        "$\r\n",
        "*\r\n"
    };

    for (const auto& header : malformed_headers) {
        ParseResult r1 = RespParser::parse(header);
        (void)r1;

        ParseResult r2 = RespParser::parse_command(header);
        (void)r2;
    }
}

// 3. Fuzz deep array nesting to ensure recursion limit stops stack overflow
TEST_F(RespParserFuzzTest, DeepRecursionStackProtection) {
    std::string deep_array;
    for (int i = 0; i < 200; ++i) {
        deep_array += "*1\r\n";
    }
    deep_array += "+leaf\r\n";

    ParseResult res = RespParser::parse(deep_array);
    // Should gracefully fail with recursion error without crashing or segfaulting
    EXPECT_EQ(res.status, ParseStatus::Error);
    EXPECT_NE(res.error_message.find("recursion"), std::string::npos);
}

// 4. Mutation fuzzer: generate valid commands and randomly corrupt bytes
TEST_F(RespParserFuzzTest, MutatedValidRespCommands) {
    const std::vector<std::string> valid_samples = {
        "*3\r\n$3\r\nSET\r\n$4\r\nuser\r\n$5\r\nAlice\r\n",
        "*2\r\n$3\r\nGET\r\n$4\r\nuser\r\n",
        "*1\r\n$4\r\nINFO\r\n",
        "*2\r\n$6\r\nEXPIRE\r\n$4\r\nuser\r\n",
        "PING\r\n",
        "SET key value\r\n",
        "KEYS *\r\n"
    };

    Store store;
    constexpr int kMutationsPerSample = 500;

    for (const auto& sample : valid_samples) {
        for (int m = 0; m < kMutationsPerSample; ++m) {
            std::string mutated = sample;
            // Introduce 1 to 5 random byte alterations/insertions/deletions
            int edits = std::uniform_int_distribution<int>(1, 5)(rng_);
            for (int e = 0; e < edits; ++e) {
                if (mutated.empty()) break;
                int op = std::uniform_int_distribution<int>(0, 2)(rng_);
                size_t pos = std::uniform_int_distribution<size_t>(0, mutated.size() - 1)(rng_);
                if (op == 0) {
                    // Replace byte
                    mutated[pos] = static_cast<char>(std::uniform_int_distribution<int>(0, 255)(rng_));
                } else if (op == 1) {
                    // Delete byte
                    mutated.erase(pos, 1);
                } else {
                    // Insert random byte
                    mutated.insert(pos, 1, static_cast<char>(std::uniform_int_distribution<int>(0, 255)(rng_)));
                }
            }

            ParseResult res = RespParser::parse_command(mutated);
            if (res.status == ParseStatus::Ok && res.value.type == RespType::Array) {
                // If it happened to parse as valid, feed it to the command dispatcher!
                std::vector<std::string> args;
                for (const auto& elem : res.value.elements) {
                    if (elem.type == RespType::BulkString || elem.type == RespType::SimpleString) {
                        args.push_back(elem.str_val);
                    }
                }
                if (!args.empty()) {
                    CommandResult cr = CommandDispatcher::execute(args, store);
                    (void)cr;
                }
            }
        }
    }
}
