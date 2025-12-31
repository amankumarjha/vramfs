// Minimal NBD server skeleton (scaffold).
// This is NOT a full NBD implementation. It's a small TCP server template
// that will be extended to implement the NBD protocol or used as a basis
// for an nbdkit plugin.

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static std::atomic<bool> running(true);

void handle_client(int client_fd) {
    std::cerr << "nbd_server: client connected" << std::endl;
    // For now, just read and discard data until the client closes.
    char buf[4096];
    while (true) {
        ssize_t r = read(client_fd, buf, sizeof(buf));
        if (r <= 0) break;
        // In a real server we'd parse NBD requests and respond with data
    }
    close(client_fd);
    std::cerr << "nbd_server: client disconnected" << std::endl;
}

int main(int argc, char** argv) {
    int port = 10809; // default
    if (argc >= 2) port = atoi(argv[1]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    if (listen(sock, 4) < 0) {
        perror("listen");
        close(sock);
        return 1;
    }

    std::cerr << "nbd_server: listening on port " << port << std::endl;

    while (running.load()) {
        int client = accept(sock, nullptr, nullptr);
        if (client < 0) continue;
        std::thread(handle_client, client).detach();
    }

    close(sock);
    return 0;
}
