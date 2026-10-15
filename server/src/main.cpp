#include "miniredis/aof.h"
#include "miniredis/server.h"
#include "miniredis/store.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
    int port = 6379;
    std::string host = "127.0.0.1";
    std::string aof_file;

    // Check environment variable
    if (const char* env_aof = std::getenv("MINIREDIS_AOF")) {
        aof_file = env_aof;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--aof" && i + 1 < argc) {
            aof_file = argv[++i];
        } else if (i == 1 && arg[0] != '-') {
            try {
                port = std::stoi(arg);
            } catch (...) {
                std::cerr << "Usage: " << argv[0] << " [port] [bind_host] [--aof <filename>]\n";
                return 1;
            }
        } else if (i == 2 && arg[0] != '-') {
            host = arg;
        } else if (i == 3 && arg[0] != '-') {
            aof_file = arg;
        }
    }

    miniredis::Store store;
    std::unique_ptr<miniredis::AofManager> aof;
    if (!aof_file.empty()) {
        std::cout << "[MiniRedis] Initializing AOF persistence with file: " << aof_file << "\n";
        aof = std::make_unique<miniredis::AofManager>(aof_file);
    }

    miniredis::Server server(host, port, store, aof.get());

    std::cout << "Starting MiniRedis on " << host << ":" << port << "...\n";
    if (!server.start()) {
        std::cerr << "Failed to start server.\n";
        return 1;
    }

    std::cout << "MiniRedis terminated cleanly.\n";
    return 0;
}
