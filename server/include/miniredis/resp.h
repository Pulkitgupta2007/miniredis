#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace miniredis {

enum class RespType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    Null
};

struct RespValue {
    RespType type{RespType::Null};
    std::string str_val{};
    int64_t int_val{0};
    std::vector<RespValue> elements{};

    static RespValue make_simple_string(std::string s) {
        RespValue v;
        v.type = RespType::SimpleString;
        v.str_val = std::move(s);
        return v;
    }

    static RespValue make_error(std::string err) {
        RespValue v;
        v.type = RespType::Error;
        v.str_val = std::move(err);
        return v;
    }

    static RespValue make_integer(int64_t n) {
        RespValue v;
        v.type = RespType::Integer;
        v.int_val = n;
        return v;
    }

    static RespValue make_bulk_string(std::string s) {
        RespValue v;
        v.type = RespType::BulkString;
        v.str_val = std::move(s);
        return v;
    }

    static RespValue make_null() {
        RespValue v;
        v.type = RespType::Null;
        return v;
    }

    static RespValue make_array(std::vector<RespValue> elems) {
        RespValue v;
        v.type = RespType::Array;
        v.elements = std::move(elems);
        return v;
    }
};

enum class ParseStatus {
    Ok,
    Incomplete,
    Error
};

struct ParseResult {
    ParseStatus status{ParseStatus::Incomplete};
    RespValue value{};
    size_t bytes_consumed{0};
    std::string error_message{};
};

class RespParser {
public:
    // Parses standard RESP2 frames (SimpleString, Error, Integer, BulkString, Array, Null)
    static ParseResult parse(std::string_view input);

    // Parses a client command, supporting both standard RESP arrays and plaintext inline commands
    static ParseResult parse_command(std::string_view input);

private:
    static ParseResult parse_internal(std::string_view input, size_t depth);
    static ParseResult parse_inline(std::string_view input);
};

class RespSerializer {
public:
    static std::string serialize(const RespValue& val);
    static std::string serialize_simple_string(std::string_view s);
    static std::string serialize_error(std::string_view err);
    static std::string serialize_integer(int64_t val);
    static std::string serialize_bulk_string(std::string_view s);
    static std::string serialize_null();
    static std::string serialize_array(const std::vector<std::string>& bulk_strings);
};

} // namespace miniredis
