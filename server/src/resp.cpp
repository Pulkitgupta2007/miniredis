#include "miniredis/resp.h"

#include <charconv>
#include <sstream>

namespace miniredis {

namespace {

constexpr size_t kMaxRecursionDepth = 64;

std::optional<int64_t> parse_int64(std::string_view sv) {
    if (sv.empty()) {
        return std::nullopt;
    }
    int64_t val = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
        return std::nullopt;
    }
    return val;
}

} // namespace

ParseResult RespParser::parse(std::string_view input) {
    return parse_internal(input, 0);
}

ParseResult RespParser::parse_command(std::string_view input) {
    if (input.empty()) {
        ParseResult res;
        res.status = ParseStatus::Incomplete;
        return res;
    }

    // Standard RESP array format
    if (input.front() == '*') {
        return parse(input);
    }

    // Inline command format (e.g., netcat or plaintext `PING\r\n`)
    return parse_inline(input);
}

ParseResult RespParser::parse_inline(std::string_view input) {
    ParseResult res;
    size_t newline_pos = input.find('\n');
    if (newline_pos == std::string_view::npos) {
        res.status = ParseStatus::Incomplete;
        return res;
    }

    size_t line_len = newline_pos;
    if (line_len > 0 && input[line_len - 1] == '\r') {
        --line_len;
    }

    std::string_view line = input.substr(0, line_len);
    std::vector<RespValue> tokens;

    size_t i = 0;
    while (i < line.size()) {
        // Skip whitespace
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }

        size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        tokens.push_back(RespValue::make_bulk_string(std::string(line.substr(start, i - start))));
    }

    res.status = ParseStatus::Ok;
    res.value = RespValue::make_array(std::move(tokens));
    res.bytes_consumed = newline_pos + 1; // Include '\n'
    return res;
}

ParseResult RespParser::parse_internal(std::string_view input, size_t depth) {
    ParseResult res;
    if (depth > kMaxRecursionDepth) {
        res.status = ParseStatus::Error;
        res.error_message = "Protocol error: maximum recursion depth exceeded";
        return res;
    }

    if (input.empty()) {
        res.status = ParseStatus::Incomplete;
        return res;
    }

    const char type_char = input.front();

    switch (type_char) {
        case '+': { // Simple String: +<str>\r\n
            size_t crlf = input.find("\r\n");
            if (crlf == std::string_view::npos) {
                res.status = ParseStatus::Incomplete;
                return res;
            }
            res.status = ParseStatus::Ok;
            res.value = RespValue::make_simple_string(std::string(input.substr(1, crlf - 1)));
            res.bytes_consumed = crlf + 2;
            return res;
        }
        case '-': { // Error: -<err>\r\n
            size_t crlf = input.find("\r\n");
            if (crlf == std::string_view::npos) {
                res.status = ParseStatus::Incomplete;
                return res;
            }
            res.status = ParseStatus::Ok;
            res.value = RespValue::make_error(std::string(input.substr(1, crlf - 1)));
            res.bytes_consumed = crlf + 2;
            return res;
        }
        case ':': { // Integer: :<num>\r\n
            size_t crlf = input.find("\r\n");
            if (crlf == std::string_view::npos) {
                res.status = ParseStatus::Incomplete;
                return res;
            }
            auto num_opt = parse_int64(input.substr(1, crlf - 1));
            if (!num_opt.has_value()) {
                res.status = ParseStatus::Error;
                res.error_message = "Protocol error: invalid integer in RESP frame";
                return res;
            }
            res.status = ParseStatus::Ok;
            res.value = RespValue::make_integer(*num_opt);
            res.bytes_consumed = crlf + 2;
            return res;
        }
        case '$': { // Bulk String: $<len>\r\n<data>\r\n
            size_t crlf = input.find("\r\n");
            if (crlf == std::string_view::npos) {
                res.status = ParseStatus::Incomplete;
                return res;
            }

            auto len_opt = parse_int64(input.substr(1, crlf - 1));
            if (!len_opt.has_value()) {
                res.status = ParseStatus::Error;
                res.error_message = "Protocol error: invalid bulk string length";
                return res;
            }

            int64_t len = *len_opt;
            if (len == -1) {
                // Null bulk string: $-1\r\n
                res.status = ParseStatus::Ok;
                res.value = RespValue::make_null();
                res.bytes_consumed = crlf + 2;
                return res;
            }

            if (len < -1) {
                res.status = ParseStatus::Error;
                res.error_message = "Protocol error: negative bulk string length";
                return res;
            }

            size_t header_size = crlf + 2;
            size_t total_needed = header_size + static_cast<size_t>(len) + 2;
            if (input.size() < total_needed) {
                res.status = ParseStatus::Incomplete;
                return res;
            }

            // Check trailing CRLF
            if (input[header_size + len] != '\r' || input[header_size + len + 1] != '\n') {
                res.status = ParseStatus::Error;
                res.error_message = "Protocol error: bulk string missing terminating CRLF";
                return res;
            }

            res.status = ParseStatus::Ok;
            res.value = RespValue::make_bulk_string(std::string(input.substr(header_size, len)));
            res.bytes_consumed = total_needed;
            return res;
        }
        case '*': { // Array: *<count>\r\n<element 1>...
            size_t crlf = input.find("\r\n");
            if (crlf == std::string_view::npos) {
                res.status = ParseStatus::Incomplete;
                return res;
            }

            auto count_opt = parse_int64(input.substr(1, crlf - 1));
            if (!count_opt.has_value()) {
                res.status = ParseStatus::Error;
                res.error_message = "Protocol error: invalid array count";
                return res;
            }

            int64_t count = *count_opt;
            if (count == -1) {
                // Null array: *-1\r\n
                res.status = ParseStatus::Ok;
                res.value = RespValue::make_null();
                res.bytes_consumed = crlf + 2;
                return res;
            }

            if (count < -1) {
                res.status = ParseStatus::Error;
                res.error_message = "Protocol error: negative array length";
                return res;
            }

            size_t consumed = crlf + 2;
            std::vector<RespValue> elements;
            elements.reserve(static_cast<size_t>(count));

            for (int64_t i = 0; i < count; ++i) {
                if (consumed >= input.size()) {
                    res.status = ParseStatus::Incomplete;
                    res.bytes_consumed = 0;
                    return res;
                }

                auto sub_res = parse_internal(input.substr(consumed), depth + 1);
                if (sub_res.status == ParseStatus::Incomplete) {
                    res.status = ParseStatus::Incomplete;
                    res.bytes_consumed = 0;
                    return res;
                }
                if (sub_res.status == ParseStatus::Error) {
                    return sub_res;
                }

                consumed += sub_res.bytes_consumed;
                elements.push_back(std::move(sub_res.value));
            }

            res.status = ParseStatus::Ok;
            res.value = RespValue::make_array(std::move(elements));
            res.bytes_consumed = consumed;
            return res;
        }
        default:
            res.status = ParseStatus::Error;
            res.error_message = "Protocol error: unexpected type prefix character";
            return res;
    }
}

std::string RespSerializer::serialize_simple_string(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 3);
    out.push_back('+');
    out.append(s);
    out.append("\r\n");
    return out;
}

std::string RespSerializer::serialize_error(std::string_view err) {
    std::string out;
    out.reserve(err.size() + 3);
    out.push_back('-');
    out.append(err);
    out.append("\r\n");
    return out;
}

std::string RespSerializer::serialize_integer(int64_t val) {
    return ":" + std::to_string(val) + "\r\n";
}

std::string RespSerializer::serialize_bulk_string(std::string_view s) {
    std::string len_str = std::to_string(s.size());
    std::string out;
    out.reserve(1 + len_str.size() + 2 + s.size() + 2);
    out.push_back('$');
    out.append(len_str);
    out.append("\r\n");
    out.append(s);
    out.append("\r\n");
    return out;
}

std::string RespSerializer::serialize_null() {
    return "$-1\r\n";
}

std::string RespSerializer::serialize_null_array() {
    return "*-1\r\n";
}

std::string RespSerializer::serialize_array(const std::vector<std::string>& bulk_strings) {
    std::string out;
    out.push_back('*');
    out.append(std::to_string(bulk_strings.size()));
    out.append("\r\n");
    for (const auto& s : bulk_strings) {
        out.append(serialize_bulk_string(s));
    }
    return out;
}

std::string RespSerializer::serialize(const RespValue& val) {
    switch (val.type) {
        case RespType::SimpleString:
            return serialize_simple_string(val.str_val);
        case RespType::Error:
            return serialize_error(val.str_val);
        case RespType::Integer:
            return serialize_integer(val.int_val);
        case RespType::BulkString:
            return serialize_bulk_string(val.str_val);
        case RespType::Null:
            return serialize_null();
        case RespType::Array: {
            std::string out;
            out.push_back('*');
            out.append(std::to_string(val.elements.size()));
            out.append("\r\n");
            for (const auto& elem : val.elements) {
                out.append(serialize(elem));
            }
            return out;
        }
    }
    return serialize_error("ERR unknown serialization type");
}

} // namespace miniredis
