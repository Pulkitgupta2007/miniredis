#pragma once

#include "miniredis/store.h"
#include <string>
#include <vector>

namespace miniredis {

struct CommandResult {
    std::string response;
    bool should_close{false};
};

class CommandDispatcher {
public:
    static CommandResult execute(const std::vector<std::string>& args, Store& store, size_t connected_clients = 1);

private:
    static std::string to_upper(std::string_view s);
};

} // namespace miniredis
