#pragma once

#include <string>
#include <deque>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

// =============================================================================
// TcpClientNB  —  Non-blocking TCP client for proxy connections
//
//   Designed for integration with a select()-based event loop.
//   Manages its own connection state and buffers.
// =============================================================================
class TcpClientNB {
public:
    TcpClientNB() : fd(-1), connected(false), connecting(false), pendingClose(false) {}
    
    ~TcpClientNB() { disconnect(); }

    // Initiate non-blocking connection. Returns immediately.
    void connect(const string& host, uint16_t port) {
        if (fd >= 0) disconnect();

        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        string portStr = to_string(port);
        int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
        if (rc != 0) {
            lastError = "getaddrinfo(): " + string(gai_strerror(rc));
            return;
        }

        fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(res);
            lastError = "socket(): " + string(strerror(errno));
            return;
        }

        setNonBlocking(fd);

        if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
            if (errno != EINPROGRESS) {
                freeaddrinfo(res);
                lastError = "connect(): " + string(strerror(errno));
                ::close(fd);
                fd = -1;
                return;
            }
            connecting = true;
        } else {
            connected = true;
        }

        freeaddrinfo(res);
    }

    void disconnect() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
        connected = false;
        connecting = false;
        pendingClose = false;
        recvBuf.clear();
        sendQueue.clear();
    }

    void send(const string& data) {
        if (fd < 0 || (!connected && !connecting)) return;
        sendQueue.insert(sendQueue.end(), data.begin(), data.end());
    }

    bool handleWrite() {
        if (connecting) {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                lastError = error != 0 ? "connect(): " + string(strerror(error)) : "connect(): " + string(strerror(errno));
                disconnect();
                return false;
            }
            connecting = false;
            connected = true;
            return true;
        }

        if (!connected || sendQueue.empty()) return false;

        string buf(sendQueue.begin(), sendQueue.end());
        ssize_t n = ::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
            lastError = "send(): " + string(strerror(errno));
            disconnect();
            return false;
        }
        sendQueue.erase(sendQueue.begin(), sendQueue.begin() + n);

        if (pendingClose && sendQueue.empty())
            disconnect();

        return true;
    }

    ssize_t handleRead() {
        if (!connected) return -1;

        char buf[4096];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
            lastError = "recv(): " + string(strerror(errno));
            disconnect();
            return -1;
        }
        if (n == 0) {
            disconnect();
            return 0;
        }
        recvBuf.append(buf, n);
        return n;
    }

    const string& peekReceived() const { return recvBuf; }
    void consumeReceived(size_t n) {
        if (n >= recvBuf.size()) recvBuf.clear();
        else recvBuf.erase(0, n);
    }

    void closeAfterFlush() {
        if (sendQueue.empty()) disconnect();
        else pendingClose = true;
    }

    int getFd() const { return fd; }
    bool isConnected() const { return connected; }
    bool isConnecting() const { return connecting; }
    bool hasDataToSend() const { return !sendQueue.empty(); }
    const string& getLastError() const { return lastError; }

private:
    int fd;
    bool connected;
    bool connecting;
    bool pendingClose;
    string recvBuf;
    deque<char> sendQueue;
    string lastError;

    static void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
};
