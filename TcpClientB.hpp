#pragma once

#include <string>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include "cpptools/misc/ERROR.hpp"

using namespace std;

// =============================================================================
// TcpClientB  –  simple blocking client intended for testing.
//
//   TcpClient c;
//   c.connect("localhost", 9090);
//   c.send("Hello!");
//   while (!c.available());
//   string reply = c.read();   // reads one complete line
//   c.disconnect();
// =============================================================================
class TcpClientB {
public:
    TcpClientB() : fd(-1) {}

    ~TcpClientB() { disconnect(); }

    // Resolve host and connect (blocking)
    void connect(const string& host, uint16_t port) {
        if (fd >= 0) disconnect();

        addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        string portStr = to_string(port);
        int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
        if (rc != 0)
            throw ERROR("getaddrinfo(): " + string(gai_strerror(rc)));

        fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) { freeaddrinfo(res); throw ERROR("socket(): " + string(strerror(errno))); }

        if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
            freeaddrinfo(res);
            throw ERROR("connect(): " + string(strerror(errno)));
        }
        freeaddrinfo(res);
    }

    void disconnect() {
        if (fd >= 0) { ::close(fd); fd = -1; }
        readBuf.clear();
    }

    // Send a message terminated with '\n'
    void send(const string& msg) {
        requireConnected();
        string data = msg + "\n";
        size_t sent = 0;
        while (sent < data.size()) {
            ssize_t n = ::send(fd, data.c_str() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n < 0) throw ERROR("send(): " + string(strerror(errno)));
            sent += static_cast<size_t>(n);
        }
    }

    // Returns true if at least one complete line is waiting in the buffer.
    // Non-blocking: drains the socket into an internal buffer, then checks.
    bool available() {
        requireConnected();
        drainSocket();
        return readBuf.find('\n') != string::npos;
    }

    // Read one complete line (blocks until '\n' arrives).
    // Strips the trailing newline (and '\r' if present).
    string read() {
        requireConnected();

        // Block until we have a complete line
        while (readBuf.find('\n') == string::npos) {
            char buf[1024];
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0) throw ERROR("recv(): " + string(strerror(errno)));
            if (n == 0) throw ERROR("Connection closed by peer");
            readBuf.append(buf, n);
        }

        size_t pos  = readBuf.find('\n');
        string line = readBuf.substr(0, pos);
        readBuf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    }

    bool isConnected() const { return fd >= 0; }

private:
    int fd;
    string readBuf;

    // Drain whatever is currently available in the socket (non-blocking peek)
    void drainSocket() {
        // Temporarily set non-blocking
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        char buf[1024];
        while (true) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n > 0) { readBuf.append(buf, n); continue; }
            break; // EAGAIN or error
        }

        // Restore original flags
        fcntl(fd, F_SETFL, flags);
    }

    void requireConnected() const {
        if (fd < 0) throw ERROR("TcpClient: not connected");
    }
};
