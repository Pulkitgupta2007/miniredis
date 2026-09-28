#include "miniredis/server.h"
#include "miniredis/store.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    int port = 6379;
    std::string host = "127.0.0.1";

    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Usage: " << argv[0] << " [port] [bind_host]\n";
            return 1;
        }
    }
    if (argc > 2) {
        host = argv[2];
    }

    miniredis::Store store;
    miniredis::Server server(host, port, store);

    std::cout << "Starting MiniRedis on " << host << ":" << port << "...\n";
    if (!server.start()) {
        std::cerr << "Failed to start server.\n";
        return 1;
    }

    std::cout << "MiniRedis terminated cleanly.\n";
    return 0;
}
