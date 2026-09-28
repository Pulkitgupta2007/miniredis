#include "miniredis/resp.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace miniredis;

TEST(RespParserTest, SimpleString) {
    auto res = RespParser::parse("+OK\r\n");
    EXPECT_EQ(res.status, ParseStatus::Ok);
    EXPECT_EQ(res.value.type, RespType::SimpleString);
    EXPECT_EQ(res.value.str_val, "OK");
    EXPECT_EQ(res.bytes_consumed, 5u);
}

TEST(RespParserTest, ErrorString) {
    auto res = RespParser::parse("-ERR unknown command\r\n");
    EXPECT_EQ(res.status, ParseStatus::Ok);
    EXPECT_EQ(res.value.type, RespType::Error);
    EXPECT_EQ(res.value.str_val, "ERR unknown command");
    EXPECT_EQ(res.bytes_consumed, 22u);
}

TEST(RespParserTest, Integer) {
    auto res = RespParser::parse(":1000\r\n");
    EXPECT_EQ(res.status, ParseStatus::Ok);
    EXPECT_EQ(res.value.type, RespType::Integer);
    EXPECT_EQ(res.value.int_val, 1000);
    EXPECT_EQ(res.bytes_consumed, 7u);

    // Negative integer
    auto res_neg = RespParser::parse(":-42\r\n");
    EXPECT_EQ(res_neg.status, ParseStatus::Ok);
    EXPECT_EQ(res_neg.value.int_val, -42);
}

TEST(RespParserTest, BulkString) {
    auto res = RespParser::parse("$5\r\nhello\r\n");
    EXPECT_EQ(res.status, ParseStatus::Ok);
    EXPECT_EQ(res.value.type, RespType::BulkString);
    EXPECT_EQ(res.value.str_val, "hello");
    EXPECT_EQ(res.bytes_consumed, 11u);

    // Empty bulk string
    auto res_empty = RespParser::parse("$0\r\n\r\n");
    EXPECT_EQ(res_empty.status, ParseStatus::Ok);
    EXPECT_EQ(res_empty.value.str_val, "");
    EXPECT_EQ(res_empty.bytes_consumed, 6u);

    // Null bulk string
    auto res_null = RespParser::parse("$-1\r\n");
    EXPECT_EQ(res_null.status, ParseStatus::Ok);
    EXPECT_EQ(res_null.value.type, RespType::Null);
    EXPECT_EQ(res_null.bytes_consumed, 5u);
}

TEST(RespParserTest, ArrayOfBulkStrings) {
    auto res = RespParser::parse("*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n");
    EXPECT_EQ(res.status, ParseStatus::Ok);
    EXPECT_EQ(res.value.type, RespType::Array);
    ASSERT_EQ(res.value.elements.size(), 2u);
    EXPECT_EQ(res.value.elements[0].str_val, "ECHO");
    EXPECT_EQ(res.value.elements[1].str_val, "hello");
}

TEST(RespParserTest, FragmentedInput) {
    // Simulate TCP stream arriving in 4 small fragments
    std::string full_packet = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";

    // Fragment 1: only array count
    std::string frag1 = "*3\r";
    EXPECT_EQ(RespParser::parse(frag1).status, ParseStatus::Incomplete);

    // Fragment 2: up to first bulk string header
    std::string frag2 = "*3\r\n$3\r\nSE";
    EXPECT_EQ(RespParser::parse(frag2).status, ParseStatus::Incomplete);

    // Fragment 3: missing trailing CRLF of last bulk string
    std::string frag3 = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar";
    EXPECT_EQ(RespParser::parse(frag3).status, ParseStatus::Incomplete);

    // Fragment 4: full packet arrives
    auto res = RespParser::parse(full_packet);
    EXPECT_EQ(res.status, ParseStatus::Ok);
    EXPECT_EQ(res.bytes_consumed, full_packet.size());
    ASSERT_EQ(res.value.elements.size(), 3u);
    EXPECT_EQ(res.value.elements[0].str_val, "SET");
    EXPECT_EQ(res.value.elements[1].str_val, "foo");
    EXPECT_EQ(res.value.elements[2].str_val, "bar");
}

TEST(RespParserTest, PipelinedCommands) {
    // Two commands packed back-to-back in a single buffer
    std::string stream = "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$1\r\nk\r\n";

    // Parse command 1
    auto res1 = RespParser::parse_command(stream);
    EXPECT_EQ(res1.status, ParseStatus::Ok);
    ASSERT_EQ(res1.value.elements.size(), 1u);
    EXPECT_EQ(res1.value.elements[0].str_val, "PING");

    // Advance buffer
    std::string remaining = stream.substr(res1.bytes_consumed);

    // Parse command 2
    auto res2 = RespParser::parse_command(remaining);
    EXPECT_EQ(res2.status, ParseStatus::Ok);
    ASSERT_EQ(res2.value.elements.size(), 2u);
    EXPECT_EQ(res2.value.elements[0].str_val, "GET");
    EXPECT_EQ(res2.value.elements[1].str_val, "k");
}

TEST(RespParserTest, InlineCommands) {
    // Plaintext command terminated by \r\n (like telnet or nc)
    std::string inline_ping = "PING\r\n";
    auto res1 = RespParser::parse_command(inline_ping);
    EXPECT_EQ(res1.status, ParseStatus::Ok);
    ASSERT_EQ(res1.value.elements.size(), 1u);
    EXPECT_EQ(res1.value.elements[0].str_val, "PING");

    // Inline command with spaces: SET mykey myvalue
    std::string inline_set = "  SET   mykey   myvalue  \r\n";
    auto res2 = RespParser::parse_command(inline_set);
    EXPECT_EQ(res2.status, ParseStatus::Ok);
    ASSERT_EQ(res2.value.elements.size(), 3u);
    EXPECT_EQ(res2.value.elements[0].str_val, "SET");
    EXPECT_EQ(res2.value.elements[1].str_val, "mykey");
    EXPECT_EQ(res2.value.elements[2].str_val, "myvalue");

    // Incomplete inline command (no newline yet)
    std::string incomplete = "PING";
    EXPECT_EQ(RespParser::parse_command(incomplete).status, ParseStatus::Incomplete);
}

TEST(RespParserTest, MalformedInput) {
    // Invalid length: non-integer
    EXPECT_EQ(RespParser::parse("$abc\r\n").status, ParseStatus::Error);

    // Invalid integer frame
    EXPECT_EQ(RespParser::parse(":notanumber\r\n").status, ParseStatus::Error);

    // Missing trailing CRLF after bulk string
    EXPECT_EQ(RespParser::parse("$3\r\nfooxx").status, ParseStatus::Error);

    // Negative array length other than -1
    EXPECT_EQ(RespParser::parse("*-5\r\n").status, ParseStatus::Error);

    // Unknown prefix char
    EXPECT_EQ(RespParser::parse("?invalid\r\n").status, ParseStatus::Error);
}

TEST(RespParserTest, HugeBulkString) {
    // 5 MB bulk string payload
    const size_t payload_len = 5 * 1024 * 1024;
    std::string payload(payload_len, 'Z');
    std::string frame = "$" + std::to_string(payload_len) + "\r\n" + payload + "\r\n";

    auto res = RespParser::parse(frame);
    EXPECT_EQ(res.status, ParseStatus::Ok);
    EXPECT_EQ(res.value.type, RespType::BulkString);
    EXPECT_EQ(res.value.str_val.size(), payload_len);
    EXPECT_EQ(res.bytes_consumed, frame.size());

    // Serialization verification of huge bulk string
    std::string serialized = RespSerializer::serialize_bulk_string(payload);
    EXPECT_EQ(serialized.size(), frame.size());
}
