#include "miniredis/command.h"
#include "miniredis/resp.h"

#include <cctype>
#include <charconv>
#include <iomanip>
#include <sstream>

namespace miniredis {

std::string CommandDispatcher::to_upper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

CommandResult CommandDispatcher::execute(const std::vector<std::string>& args, Store& store, size_t connected_clients) {
    CommandResult cr;
    if (args.empty()) {
        cr.response = RespSerializer::serialize_error("ERR empty command");
        return cr;
    }

    store.metrics().record_command();
    std::string cmd = to_upper(args[0]);

    if (cmd == "PING") {
        if (args.size() > 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'ping' command");
            return cr;
        }
        if (args.size() == 2) {
            cr.response = RespSerializer::serialize_bulk_string(args[1]);
        } else {
            cr.response = RespSerializer::serialize_simple_string("PONG");
        }
        return cr;
    }

    if (cmd == "ECHO") {
        if (args.size() != 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'echo' command");
            return cr;
        }
        cr.response = RespSerializer::serialize_bulk_string(args[1]);
        return cr;
    }

    if (cmd == "SET") {
        if (args.size() < 3) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'set' command");
            return cr;
        }

        const std::string& key = args[1];
        const std::string& val = args[2];
        std::optional<std::chrono::milliseconds> ttl = std::nullopt;
        SetCondition cond = SetCondition::NONE;

        for (size_t i = 3; i < args.size(); ++i) {
            std::string opt = to_upper(args[i]);
            if (opt == "EX") {
                if (++i >= args.size()) {
                    cr.response = RespSerializer::serialize_error("ERR syntax error");
                    return cr;
                }
                int64_t sec = 0;
                auto [ptr, ec] = std::from_chars(args[i].data(), args[i].data() + args[i].size(), sec);
                if (ec != std::errc() || ptr != args[i].data() + args[i].size() || sec <= 0) {
                    cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
                    return cr;
                }
                ttl = std::chrono::seconds(sec);
            } else if (opt == "PX") {
                if (++i >= args.size()) {
                    cr.response = RespSerializer::serialize_error("ERR syntax error");
                    return cr;
                }
                int64_t ms = 0;
                auto [ptr, ec] = std::from_chars(args[i].data(), args[i].data() + args[i].size(), ms);
                if (ec != std::errc() || ptr != args[i].data() + args[i].size() || ms <= 0) {
                    cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
                    return cr;
                }
                ttl = std::chrono::milliseconds(ms);
            } else if (opt == "NX") {
                cond = SetCondition::NX;
            } else if (opt == "XX") {
                cond = SetCondition::XX;
            } else {
                cr.response = RespSerializer::serialize_error("ERR syntax error");
                return cr;
            }
        }

        SetResult sr = store.set(key, val, ttl, cond);
        if (sr.oom) {
            cr.response = RespSerializer::serialize_error("OOM command not allowed when used memory > 'maxmemory'");
        } else if (sr.ok) {
            cr.response = RespSerializer::serialize_simple_string("OK");
        } else {
            cr.response = RespSerializer::serialize_null();
        }
        return cr;
    }

    if (cmd == "GET") {
        if (args.size() != 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'get' command");
            return cr;
        }
        auto val = store.get(args[1]);
        if (val.has_value()) {
            cr.response = RespSerializer::serialize_bulk_string(*val);
        } else {
            cr.response = RespSerializer::serialize_null();
        }
        return cr;
    }

    if (cmd == "DEL") {
        if (args.size() < 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'del' command");
            return cr;
        }
        std::vector<std::string> keys(args.begin() + 1, args.end());
        size_t count = store.del(keys);
        cr.response = RespSerializer::serialize_integer(static_cast<int64_t>(count));
        return cr;
    }

    if (cmd == "EXISTS") {
        if (args.size() < 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'exists' command");
            return cr;
        }
        std::vector<std::string> keys(args.begin() + 1, args.end());
        size_t count = store.exists(keys);
        cr.response = RespSerializer::serialize_integer(static_cast<int64_t>(count));
        return cr;
    }

    if (cmd == "KEYS") {
        if (args.size() != 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'keys' command");
            return cr;
        }
        auto matched = store.keys(args[1]);
        cr.response = RespSerializer::serialize_array(matched);
        return cr;
    }

    if (cmd == "SCAN") {
        if (args.size() < 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'scan' command");
            return cr;
        }

        size_t cursor = 0;
        auto [ptr, ec] = std::from_chars(args[1].data(), args[1].data() + args[1].size(), cursor);
        if (ec != std::errc() || ptr != args[1].data() + args[1].size()) {
            cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
            return cr;
        }

        std::string pattern = "*";
        size_t count = 10;

        for (size_t i = 2; i < args.size(); i += 2) {
            if (i + 1 >= args.size()) {
                cr.response = RespSerializer::serialize_error("ERR syntax error");
                return cr;
            }
            std::string opt = to_upper(args[i]);
            if (opt == "MATCH") {
                pattern = args[i + 1];
            } else if (opt == "COUNT") {
                int64_t c = 0;
                auto [p, e] = std::from_chars(args[i + 1].data(), args[i + 1].data() + args[i + 1].size(), c);
                if (e == std::errc() && p == args[i + 1].data() + args[i + 1].size() && c > 0) {
                    count = static_cast<size_t>(c);
                } else {
                    cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
                    return cr;
                }
            } else {
                cr.response = RespSerializer::serialize_error("ERR syntax error");
                return cr;
            }
        }

        auto [next_cursor, matched_keys] = store.scan(cursor, pattern, count);
        std::string resp = "*2\r\n";
        resp += RespSerializer::serialize_bulk_string(std::to_string(next_cursor));
        resp += RespSerializer::serialize_array(matched_keys);
        cr.response = std::move(resp);
        return cr;
    }

    if (cmd == "DBSIZE") {
        if (args.size() != 1) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'dbsize' command");
            return cr;
        }
        cr.response = RespSerializer::serialize_integer(static_cast<int64_t>(store.dbsize()));
        return cr;
    }

    if (cmd == "FLUSHALL" || cmd == "FLUSHDB") {
        store.flushall();
        cr.response = RespSerializer::serialize_simple_string("OK");
        return cr;
    }

    if (cmd == "TTL") {
        if (args.size() != 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'ttl' command");
            return cr;
        }
        cr.response = RespSerializer::serialize_integer(store.ttl(args[1]));
        return cr;
    }

    if (cmd == "PTTL") {
        if (args.size() != 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'pttl' command");
            return cr;
        }
        cr.response = RespSerializer::serialize_integer(store.pttl(args[1]));
        return cr;
    }

    if (cmd == "EXPIRE") {
        if (args.size() != 3) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'expire' command");
            return cr;
        }
        int64_t sec = 0;
        auto [ptr, ec] = std::from_chars(args[2].data(), args[2].data() + args[2].size(), sec);
        if (ec != std::errc() || ptr != args[2].data() + args[2].size()) {
            cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
            return cr;
        }
        bool ok = store.expire(args[1], std::chrono::seconds(sec));
        cr.response = RespSerializer::serialize_integer(ok ? 1 : 0);
        return cr;
    }

    if (cmd == "PEXPIRE") {
        if (args.size() != 3) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'pexpire' command");
            return cr;
        }
        int64_t ms = 0;
        auto [ptr, ec] = std::from_chars(args[2].data(), args[2].data() + args[2].size(), ms);
        if (ec != std::errc() || ptr != args[2].data() + args[2].size()) {
            cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
            return cr;
        }
        bool ok = store.pexpire(args[1], std::chrono::milliseconds(ms));
        cr.response = RespSerializer::serialize_integer(ok ? 1 : 0);
        return cr;
    }

    if (cmd == "EXPIREAT") {
        if (args.size() != 3) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'expireat' command");
            return cr;
        }
        int64_t sec = 0;
        auto [ptr, ec] = std::from_chars(args[2].data(), args[2].data() + args[2].size(), sec);
        if (ec != std::errc() || ptr != args[2].data() + args[2].size()) {
            cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
            return cr;
        }
        bool ok = store.expireat(args[1], sec);
        cr.response = RespSerializer::serialize_integer(ok ? 1 : 0);
        return cr;
    }

    if (cmd == "PEXPIREAT") {
        if (args.size() != 3) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'pexpireat' command");
            return cr;
        }
        int64_t ms = 0;
        auto [ptr, ec] = std::from_chars(args[2].data(), args[2].data() + args[2].size(), ms);
        if (ec != std::errc() || ptr != args[2].data() + args[2].size()) {
            cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
            return cr;
        }
        bool ok = store.pexpireat(args[1], ms);
        cr.response = RespSerializer::serialize_integer(ok ? 1 : 0);
        return cr;
    }

    if (cmd == "BGREWRITEAOF") {
        cr.response = RespSerializer::serialize_simple_string("Background append only file rewriting started");
        return cr;
    }

    // Configuration & Memory commands
    if (cmd == "CONFIG") {
        if (args.size() < 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'config' command");
            return cr;
        }

        std::string sub = to_upper(args[1]);
        if (sub == "GET") {
            if (args.size() != 3) {
                cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'config|get' command");
                return cr;
            }
            std::string param = args[2];
            std::string param_upper = to_upper(param);
            if (param_upper == "MAXMEMORY") {
                cr.response = RespSerializer::serialize_array({"maxmemory", std::to_string(store.get_maxmemory())});
            } else if (param_upper == "MAXMEMORY-POLICY") {
                cr.response = RespSerializer::serialize_array({"maxmemory-policy", store.get_maxmemory_policy()});
            } else if (param_upper == "SAVE") {
                cr.response = RespSerializer::serialize_array({"save", ""});
            } else if (param_upper == "APPENDONLY") {
                cr.response = RespSerializer::serialize_array({"appendonly", "no"});
            } else {
                cr.response = "*0\r\n";
            }
            return cr;
        } else if (sub == "SET") {
            if (args.size() != 4) {
                cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'config|set' command");
                return cr;
            }
            std::string param = to_upper(args[2]);
            const std::string& val_str = args[3];
            if (param == "MAXMEMORY") {
                int64_t bytes = 0;
                auto [ptr, ec] = std::from_chars(val_str.data(), val_str.data() + val_str.size(), bytes);
                if (ec != std::errc() || ptr != val_str.data() + val_str.size() || bytes < 0) {
                    cr.response = RespSerializer::serialize_error("ERR value is not an integer or out of range");
                    return cr;
                }
                store.set_maxmemory(static_cast<size_t>(bytes));
                cr.response = RespSerializer::serialize_simple_string("OK");
            } else if (param == "MAXMEMORY-POLICY") {
                bool ok = store.set_maxmemory_policy(val_str);
                if (ok) {
                    cr.response = RespSerializer::serialize_simple_string("OK");
                } else {
                    cr.response = RespSerializer::serialize_error("ERR invalid maxmemory-policy. Options: noeviction, allkeys-lru, allkeys-lfu");
                }
            } else {
                cr.response = RespSerializer::serialize_error("ERR Unsupported CONFIG parameter: " + args[2]);
            }
            return cr;
        } else {
            cr.response = RespSerializer::serialize_error("ERR unknown subcommand for 'config'");
            return cr;
        }
    }

    if (cmd == "MEMORY") {
        if (args.size() < 2) {
            cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'memory' command");
            return cr;
        }
        std::string sub = to_upper(args[1]);
        if (sub == "USAGE") {
            if (args.size() != 3) {
                cr.response = RespSerializer::serialize_error("ERR wrong number of arguments for 'memory|usage' command");
                return cr;
            }
            auto mem = store.memory_usage(args[2]);
            if (mem.has_value()) {
                cr.response = RespSerializer::serialize_integer(static_cast<int64_t>(*mem));
            } else {
                cr.response = RespSerializer::serialize_null();
            }
            return cr;
        }
        cr.response = RespSerializer::serialize_error("ERR unknown subcommand for 'memory'");
        return cr;
    }

    if (cmd == "EVENTS") {
        size_t limit = 20;
        if (args.size() == 2) {
            int64_t val = 0;
            auto [ptr, ec] = std::from_chars(args[1].data(), args[1].data() + args[1].size(), val);
            if (ec == std::errc() && ptr == args[1].data() + args[1].size() && val > 0) {
                limit = static_cast<size_t>(val);
            }
        } else if (args.size() == 3 && to_upper(args[1]) == "LIMIT") {
            int64_t val = 0;
            auto [ptr, ec] = std::from_chars(args[2].data(), args[2].data() + args[2].size(), val);
            if (ec == std::errc() && ptr == args[2].data() + args[2].size() && val > 0) {
                limit = static_cast<size_t>(val);
            }
        }

        auto recent = store.telemetry_buffer().get_recent(limit);
        std::vector<std::string> formatted_events;
        formatted_events.reserve(recent.size());
        for (const auto& ev : recent) {
            formatted_events.push_back(ev.format());
        }
        cr.response = RespSerializer::serialize_array(formatted_events);
        return cr;
    }

    if (cmd == "LRUKEYS" || cmd == "LRU.KEYS") {
        auto keys = store.lru_keys();
        cr.response = RespSerializer::serialize_array(keys);
        return cr;
    }

    if (cmd == "LFUKEYS" || cmd == "LFU.KEYS") {
        auto keys = store.lfu_keys();
        cr.response = RespSerializer::serialize_array(keys);
        return cr;
    }

    // Compatibility commands for real redis-cli
    if (cmd == "COMMAND") {
        cr.response = "*0\r\n";
        return cr;
    }

    if (cmd == "INFO") {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        int64_t uptime_sec = store.metrics().uptime_seconds();
        int64_t uptime_days = uptime_sec / 86400;

        oss << "# Server\r\n";
        oss << "redis_version:miniredis-1.0.0\r\n";
        oss << "uptime_in_seconds:" << uptime_sec << "\r\n";
        oss << "uptime_in_days:" << uptime_days << "\r\n";
        oss << "connected_clients:" << connected_clients << "\r\n";
        oss << "\r\n";

        oss << "# Memory\r\n";
        oss << "used_memory:" << store.used_memory() << "\r\n";
        oss << "maxmemory:" << store.get_maxmemory() << "\r\n";
        oss << "maxmemory_policy:" << store.get_maxmemory_policy() << "\r\n";
        oss << "\r\n";

        oss << "# Stats\r\n";
        oss << "total_commands_processed:" << store.metrics().total_commands_processed() << "\r\n";
        oss << "ops_per_sec:" << store.metrics().ops_per_sec() << "\r\n";
        oss << "keyspace_hits:" << store.metrics().keyspace_hits() << "\r\n";
        oss << "keyspace_misses:" << store.metrics().keyspace_misses() << "\r\n";
        oss << "hit_ratio:" << store.metrics().hit_ratio() << "\r\n";
        oss << "evicted_keys:" << store.eviction_buffer().total_evictions() << "\r\n";
        oss << "expired_keys:" << store.metrics().expired_keys() << "\r\n";
        oss << "total_keys:" << store.dbsize() << "\r\n";

        cr.response = RespSerializer::serialize_bulk_string(oss.str());
        return cr;
    }

    if (cmd == "CLIENT") {
        cr.response = RespSerializer::serialize_simple_string("OK");
        return cr;
    }

    if (cmd == "QUIT") {
        cr.response = RespSerializer::serialize_simple_string("OK");
        cr.should_close = true;
        return cr;
    }

    // Unknown command
    std::string err = "ERR unknown command '" + args[0] + "', with args beginning with: ";
    for (size_t i = 1; i < args.size(); ++i) {
        err += "'" + args[i] + "' ";
    }
    cr.response = RespSerializer::serialize_error(err);
    return cr;
}

} // namespace miniredis
