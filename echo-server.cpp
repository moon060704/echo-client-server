#include <algorithm>
#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

constexpr int BUF_SIZE = 4096;

std::vector<int> clientSockets;
std::mutex clientMutex;

void usage() {
    std::cerr << "syntax : echo-server <port> [-e [-b]]\n";
    std::cerr << "sample : echo-server 1234 -e -b\n";
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

void registerClient(int sock) {
    std::lock_guard<std::mutex> lock(clientMutex);
    clientSockets.push_back(sock);
}

void unregisterClient(int sock) {
    std::lock_guard<std::mutex> lock(clientMutex);

    clientSockets.erase(
        std::remove(clientSockets.begin(), clientSockets.end(), sock),
        clientSockets.end()
    );
}

std::vector<int> getClientSnapshot() {
    std::lock_guard<std::mutex> lock(clientMutex);
    return clientSockets;
}

void sendToAllClients(const char* data, size_t len) {
    std::vector<int> targets = getClientSnapshot();

    for (int clientSock : targets) {
        sendAll(clientSock, data, len);
    }
}

void serveClient(int clientSock, bool echoMode, bool broadcastMode) {
    registerClient(clientSock);

    std::cout << "connected\n";
    std::cout.flush();

    char buf[BUF_SIZE];

    while (true) {
        ssize_t len = recv(clientSock, buf, sizeof(buf), 0);
        if (len <= 0) break;

        std::cout.write(buf, len);
        std::cout.flush();

        if (echoMode) {
            if (broadcastMode) {
                sendToAllClients(buf, len);
            } else {
                if (!sendAll(clientSock, buf, len)) break;
            }
        }
    }

    unregisterClient(clientSock);
    close(clientSock);

    std::cout << "disconnected\n";
    std::cout.flush();
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        usage();
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int port = std::atoi(argv[1]);

    if (port <= 0 || port > 65535) {
        usage();
        return 1;
    }

    bool echoMode = false;
    bool broadcastMode = false;

    for (int i = 2; i < argc; i++) {
        std::string option = argv[i];

        if (option == "-e") {
            echoMode = true;
        } else if (option == "-b") {
            broadcastMode = true;
        } else {
            usage();
            return 1;
        }
    }

    if (broadcastMode && !echoMode) {
        std::cerr << "-b option requires -e option\n";
        usage();
        return 1;
    }

    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == -1) {
        perror("socket");
        return 1;
    }

    int optval = 1;
    if (setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(listenSock);
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1) {
        perror("bind");
        close(listenSock);
        return 1;
    }

    if (listen(listenSock, 5) == -1) {
        perror("listen");
        close(listenSock);
        return 1;
    }

    while (true) {
        int clientSock = accept(listenSock, nullptr, nullptr);

        if (clientSock == -1) {
            perror("accept");
            continue;
        }

        std::thread(serveClient, clientSock, echoMode, broadcastMode).detach();
    }

    close(listenSock);

    return 0;
}
