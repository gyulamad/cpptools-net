#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <cstring>
#include <cerrno>
#include <atomic>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "../misc/ERROR.hpp"

using namespace std;

// =============================================================================
// TcpServer  —  pure transport layer
//
//   Responsibilities:
//     - Accept and track client connections
//     - Deliver raw bytes to subclasses via onRawData()
//     - Flush outbound bytes via sendToClient()
//     - Fire lifecycle hooks: connect / disconnect / error / tick
//
//   Explicitly NOT a responsibility:
//     - Any message framing (line-splitting, HTTP parsing, etc.)
//     - Any application logic
//
//   Subclass and implement onRawData() to define your protocol framing.
// =============================================================================
class TcpServer {
public:
    TcpServer() : listenFd(-1), running(false) {}
    virtual ~TcpServer() { stop(); }

    void listen(uint16_t port) {
        setupListenSocket(port);
        running = true;
        onServerStart(port);
        eventLoop();
        onServerStop();
    }

    void stop() { running = false; }

protected:
    // == Lifecycle hooks =======================================================

    // LCOV_EXCL_START

    virtual void onServerStart(uint16_t /*port*/) {}
    virtual void onServerStop() {}

    // fd         : file descriptor identifying this client
    // remoteAddr : "ip:port" string
    virtual void onClientConnect(int /*fd*/, const string& /*remoteAddr*/) {}
    virtual void onClientDisconnect(int /*fd*/) {}
    virtual void onClientError(int /*fd*/, const string& /*error*/) {}

    // Called every ~100 ms regardless of socket activity.
    // Useful for timers, drip-sending, keepalives, etc.
    virtual void onTick() {}

    // == Data hook — implement your framing here ===============================
    //
    // Called whenever bytes arrive from a client.
    // buf contains ALL unprocessed bytes for this connection (accumulates
    // across calls). Consume fully-parsed frames by erasing from the front.
    // Leave any incomplete tail in place — it will be prepended to the next
    // incoming chunk automatically.
    virtual void onRawData(int fd, string& buf) = 0;

    // LCOV_EXCL_STOP

    // == Utilities =============================================================

    void sendToClient(int fd, const string& data) {
        auto it = clients.find(fd);
        if (it != clients.end())
            it->second.sendQueue.insert(
                it->second.sendQueue.end(), data.begin(), data.end()
            );
    }

    void disconnectClient(int fd) {
        auto it = clients.find(fd);
        if (it == clients.end()) return;
        onClientDisconnect(fd);
        ::close(fd);
        clients.erase(it);
    }

    // Finish draining the send queue, then disconnect.
    // Safe to call from inside any hook — won't close mid-write.
    void closeAfterFlush(int fd) {
        auto it = clients.find(fd);
        if (it == clients.end()) return;
        if (it->second.sendQueue.empty())
            disconnectClient(fd);
        else
            it->second.pendingClose = true;
    }

    // == Protected members for subclass access =================================

    struct ClientState {
        string remoteAddr;
        string recvBuf;
        deque<char> sendQueue;
        bool pendingClose = false;
    };

    int listenFd;
    atomic<bool> running;
    unordered_map<int, ClientState> clients;

    // == Event loop - can be overridden by subclasses =========================
    virtual void eventLoop() {
        while (running) {
            fd_set readSet, writeSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);

            FD_SET(listenFd, &readSet);
            int maxFd = listenFd;

            for (auto& [fd, st] : clients) {
                FD_SET(fd, &readSet);
                if (!st.sendQueue.empty()) FD_SET(fd, &writeSet);
                if (fd > maxFd) maxFd = fd;
            }

            timeval tv{0, 100'000}; // 100 ms — keeps onTick() responsive
            int ready = ::select(maxFd + 1, &readSet, &writeSet, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR) continue;
                throw ERROR("select(): " + string(strerror(errno)));
            }

            onTick();

            if (FD_ISSET(listenFd, &readSet))
                acceptClients();

            // Snapshot fds — hooks may add/remove clients mid-iteration
            vector<int> fds;
            fds.reserve(clients.size());
            for (auto& [fd, _] : clients) fds.push_back(fd);

            for (int fd : fds) {
                if (!clients.count(fd)) continue;
                if (FD_ISSET(fd, &readSet))  handleRead(fd);
                if (!clients.count(fd)) continue;
                if (FD_ISSET(fd, &writeSet)) handleWrite(fd);
            }
        }

        for (auto& [fd, _] : clients) ::close(fd);
        clients.clear();
        if (listenFd >= 0) { ::close(listenFd); listenFd = -1; }
    }

    void acceptClients() {
        while (true) {
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            int cfd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&addr), &len);
            if (cfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                onClientError(-1, "accept(): " + string(strerror(errno)));
                break;
            }
            setNonBlocking(cfd);

            char ipBuf[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
            string remote = string(ipBuf) + ":" + to_string(ntohs(addr.sin_port));

            clients[cfd] = ClientState{remote, {}, {}, false};
            onClientConnect(cfd, remote);
        }
    }

    void handleRead(int fd) {
        char buf[4096];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            onClientError(fd, "recv(): " + string(strerror(errno)));
            disconnectClient(fd);
            return;
        }
        if (n == 0) { disconnectClient(fd); return; }

        auto& st = clients[fd];
        st.recvBuf.append(buf, n);
        onRawData(fd, st.recvBuf);
    }

    void handleWrite(int fd) {
        auto& st = clients[fd];
        if (st.sendQueue.empty()) return;

        string buf(st.sendQueue.begin(), st.sendQueue.end());
        ssize_t n = ::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            onClientError(fd, "send(): " + string(strerror(errno)));
            disconnectClient(fd);
            return;
        }
        st.sendQueue.erase(st.sendQueue.begin(), st.sendQueue.begin() + n);

        if (st.pendingClose && st.sendQueue.empty())
            disconnectClient(fd);
    }

    static void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

private:
    // == Socket setup ==========================================================
    void setupListenSocket(uint16_t port) {
        listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) throw ERROR("socket(): " + string(strerror(errno)));

        int opt = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setNonBlocking(listenFd);

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(port);

        if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw ERROR("bind(): " + string(strerror(errno)));

        if (::listen(listenFd, SOMAXCONN) < 0)
            throw ERROR("listen(): " + string(strerror(errno)));
    }
};
