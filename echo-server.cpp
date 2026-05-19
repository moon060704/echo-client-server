#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#endif // __linux__
#ifdef WIN32
#include <ws2tcpip.h>
#endif // WIN32
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>

#ifdef WIN32
void myerror(const char* msg) { fprintf(stderr, "%s %lu\n", msg, GetLastError()); }
#else
void myerror(const char* msg) { fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno); }
#endif

void usage() {
	printf("echo-server\n");
	printf("\n");
	printf("syntax: echo-server <port> [-e [-b]]\n");
	printf("  -e : echo\n");
	printf("  -b : broadcast\n");
	printf("sample: echo-server 1234 -e -b\n");
}

struct Param {
	bool echo{false};
	bool broadcast{false};
	uint16_t port{0};

	bool parse(int argc, char* argv[]) {
		for (int i = 1; i < argc;) {
			if (strcmp(argv[i], "-e") == 0) {
				echo = true;
				i++;
				continue;
			}

			if (strcmp(argv[i], "-b") == 0) {
				broadcast = true;
				i++;
				continue;
			}

			if (i < argc) port = atoi(argv[i++]);
		}

		if (broadcast && !echo) {
			fprintf(stderr, "-b requires -e\n");
			return false;
		}

		return port != 0;
	}
} param;

std::vector<int> clients;
std::mutex clientsMutex;

void recvThread(int sd) {
	printf("connected\n");
	fflush(stdout);

	static const int BUFSIZE = 65536;
	char buf[BUFSIZE];

	while (true) {
		ssize_t res = ::recv(sd, buf, BUFSIZE - 1, 0);
		if (res == 0 || res == -1) {
			fprintf(stderr, "recv return %zd", res);
			myerror(" ");
			break;
		}

		buf[res] = '\0';
		printf("%s", buf);
		fflush(stdout);

		if (param.echo) {
			if (param.broadcast) {
				std::vector<int> targets;

				{
					std::lock_guard<std::mutex> lock(clientsMutex);
					targets = clients;
				}

				for (int cli : targets) {
					::send(cli, buf, res, 0);
				}
			} else {
				ssize_t sendRes = ::send(sd, buf, res, 0);
				if (sendRes == 0 || sendRes == -1) {
					fprintf(stderr, "send return %zd", sendRes);
					myerror(" ");
					break;
				}
			}
		}
	}

	printf("disconnected\n");
	fflush(stdout);

	{
		std::lock_guard<std::mutex> lock(clientsMutex);
		clients.erase(std::remove(clients.begin(), clients.end(), sd), clients.end());
	}

	::close(sd);
}

int main(int argc, char* argv[]) {
	if (!param.parse(argc, argv)) {
		usage();
		return -1;
	}

	signal(SIGPIPE, SIG_IGN);

#ifdef WIN32
	WSAData wsaData;
	WSAStartup(0x0202, &wsaData);
#endif // WIN32

	//
	// socket
	//
	int sd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (sd == -1) {
		myerror("socket");
		return -1;
	}

#ifdef __linux__
	//
	// setsockopt
	//
	{
		int optval = 1;
		int res = ::setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
		if (res == -1) {
			myerror("setsockopt");
			return -1;
		}
	}
#endif // __linux__

	//
	// bind
	//
	{
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(param.port);

		ssize_t res = ::bind(sd, (struct sockaddr *)&addr, sizeof(addr));
		if (res == -1) {
			myerror("bind");
			return -1;
		}
	}

	//
	// listen
	//
	{
		int res = ::listen(sd, 5);
		if (res == -1) {
			myerror("listen");
			return -1;
		}
	}

	while (true) {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);

		int newsd = ::accept(sd, (struct sockaddr *)&addr, &len);
		if (newsd == -1) {
			myerror("accept");
			break;
		}

		{
			std::lock_guard<std::mutex> lock(clientsMutex);
			clients.push_back(newsd);
		}

		std::thread t(recvThread, newsd);
		t.detach();
	}

	::close(sd);
	return 0;
}
