#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

constexpr int BUF_SIZE = 4096;

void usage() {
    std::cerr << "syntax : echo-client <ip> <port>\n";
    std::cerr << "sample : echo-client 127.0.0.1 1234\n";
}

bool sendAll(int sock, const char* data, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t res = send(sock, data + sent, len - sent, 0);
        if (res <= 0) return false;
        sent += res;
    }

    return true;
}

void receiveLoop(int sock) {
    char buf[BUF_SIZE];

    while (true) {
        ssize_t len = recv(sock, buf, sizeof(buf), 0);
        if (len <= 0) break;

        std::cout.write(buf, len);
        std::cout.flush();
    }
}

void inputLoop(int sock) {
    std::string line;

    while (std::getline(std::cin, line)) {
        line += '\n';

        if (!sendAll(sock, line.data(), line.size())) {
            perror("send");
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        usage();
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    const char* ip = argv[1];
    int port = std::atoi(argv[2]);

    if (port <= 0 || port > 65535) {
        usage();
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serverAddr.sin_addr) != 1) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1) {
        perror("connect");
        close(sock);
        return 1;
    }

    std::cout << "connected\n";
    std::cout.flush();

    std::thread receiver(receiveLoop, sock);

    inputLoop(sock);

    shutdown(sock, SHUT_WR);

    if (receiver.joinable()) {
        receiver.join();
    }

    close(sock);

    return 0;
}
