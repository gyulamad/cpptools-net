#pragma once

#include "TcpServer.hpp"
#include <iostream>
#include <unordered_map>
#include <deque>
#include <vector>
#include <cstring>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include "TcpClientNB.hpp"

using namespace std;

// =============================================================================
// TcpProxy  —  TCP proxy server
//
//   Accepts client connections and forwards traffic to a backend server.
//   Each client gets its own connection to the backend.
//   Handles bidirectional forwarding with graceful disconnect handling.
//
//   Usage:
//     TcpProxy proxy;
//     proxy.forward(8080, "backend.example.com", 9090);
// =============================================================================
class TcpProxy : public TcpServer {
public:
    TcpProxy() : backendPort(0) {}
    virtual ~TcpProxy() {}

    void forward(int port, const string& host, int hport) {
        backendHost = host;
        backendPort = (uint16_t)hport;
        listen((uint16_t)port);
    }

protected:
    void onServerStart(uint16_t port) override {
        cout << "[TcpProxy] Listening on port " << port 
             << " -> forwarding to " << backendHost << ":" << backendPort << endl;
    }

    void onServerStop() override {
        backends.clear();
        cout << "[TcpProxy] Stopped." << endl;
    }

    void onClientConnect(int fd, const string& addr) override {
        cout << "[+] Client " << fd << " from " << addr << endl;
        
        auto backend = make_unique<TcpClientNB>();
        backend->connect(backendHost, backendPort);
        backends[fd] = BackendState{::move(backend), addr, {}};
    }

    void onClientDisconnect(int fd) override {
        cout << "[-] Client " << fd << " disconnected." << endl;
        
        auto it = backends.find(fd);
        if (it != backends.end()) {
            it->second.backend->disconnect();
            backends.erase(it);
        }
    }

    // LCOV_EXCL_START
    void onClientError(int fd, const string& err) override {
        cerr << "[!] Client " << fd << " error: " << err << endl;
        
        auto it = backends.find(fd);
        if (it != backends.end()) {
            it->second.backend->disconnect();
            backends.erase(it);
        }
    }
    // LCOV_EXCL_STOP

    void onRawData(int clientFd, string& buf) override {
        auto it = backends.find(clientFd);
        if (it == backends.end()) {
            buf.clear();
            return;
        }

        auto& backend = it->second.backend;
        if (!buf.empty()) {
            if (backend->isConnected()) {
                backend->send(buf);
            } else if (backend->isConnecting()) {
                // Buffer data while backend is connecting
                it->second.pendingData.append(buf);
            }
            buf.clear();
        }
    }

    // Override eventLoop to include backend fds in select
    void eventLoop() override {
        while (running) {
            fd_set readSet, writeSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);

            FD_SET(listenFd, &readSet);
            int maxFd = listenFd;

            // Client fds
            for (auto& [fd, st] : clients) {
                FD_SET(fd, &readSet);
                if (!st.sendQueue.empty()) FD_SET(fd, &writeSet);
                if (fd > maxFd) maxFd = fd;
            }

            // Backend fds
            for (auto& [clientFd, state] : backends) {
                auto& backend = state.backend;
                int bfd = backend->getFd();
                if (bfd < 0) continue;

                FD_SET(bfd, &readSet);
                if (backend->isConnecting() || backend->hasDataToSend())
                    FD_SET(bfd, &writeSet);
                if (bfd > maxFd) maxFd = bfd;
            }

            timeval tv{0, 100'000}; // 100 ms
            int ready = ::select(maxFd + 1, &readSet, &writeSet, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR) continue;
                throw ERROR("select(): " + string(strerror(errno)));
            }

            onTick();

            if (FD_ISSET(listenFd, &readSet))
                acceptClients();

            // Handle backend socket events
            processBackends(readSet, writeSet);

            // Handle client socket events
            processClients(readSet, writeSet);
        }

        cleanup();
    }

// private:
    string backendHost;
    uint16_t backendPort;

    struct BackendState {
        unique_ptr<TcpClientNB> backend;
        string clientAddr;
        string pendingData;  // Buffer for data received while connecting
    };

    unordered_map<int, BackendState> backends;

    void processBackends(fd_set& readSet, fd_set& writeSet) {
        // Snapshot client fds to avoid iterator invalidation
        vector<int> clientFds;
        clientFds.reserve(backends.size());
        for (auto& [fd, _] : backends)
            clientFds.push_back(fd);

        for (int clientFd : clientFds) {
            auto it = backends.find(clientFd);
            if (it == backends.end()) continue;

            auto& backend = it->second.backend;
            int bfd = backend->getFd();
            if (bfd < 0) {
                // Backend connection failed
                if (!backend->isConnecting()) {
                    cerr << "[!] Backend connection failed for client " << clientFd << endl;
                    disconnectClient(clientFd);
                }
                continue;
            }

            // Handle write (connection completion or data sending)
            if (FD_ISSET(bfd, &writeSet)) {
                bool wasConnecting = backend->isConnecting();
                backend->handleWrite();
                // Flush pending data when connection completes
                if (wasConnecting && backend->isConnected() && !it->second.pendingData.empty()) {
                    backend->send(it->second.pendingData);
                    it->second.pendingData.clear();
                }
            }

            // Handle read
            if (FD_ISSET(bfd, &readSet)) {
                ssize_t n = backend->handleRead();
                if (n == 0) {
                    // Backend closed connection - forward any remaining data first
                    const string& remaining = backend->peekReceived();
                    if (!remaining.empty()) {
                        sendToClient(clientFd, remaining);
                        backend->consumeReceived(remaining.size());
                    }
                    // Graceful close: let client sendQueue drain before disconnecting
                    cout << "[-] Backend closed for client " << clientFd << endl;
                    auto cit = clients.find(clientFd);
                    if (cit != clients.end() && !cit->second.sendQueue.empty()) {
                        // Data pending - close after flush
                        closeAfterFlush(clientFd);
                    } else {
                        // No pending data - close immediately
                        disconnectClient(clientFd);
                    }
                    continue;
                }
            }

            // Forward backend data to client
            if (backend->isConnected()) {
                const string& recv = backend->peekReceived();
                if (!recv.empty()) {
                    sendToClient(clientFd, recv);
                    backend->consumeReceived(recv.size());
                }
            }
        }
    }

    void processClients(fd_set& readSet, fd_set& writeSet) {
        // Snapshot fds to avoid iterator invalidation
        vector<int> fds;
        fds.reserve(clients.size());
        for (auto& [fd, _] : clients) fds.push_back(fd);

        for (int fd : fds) {
            if (!clients.count(fd)) continue;
            if (FD_ISSET(fd, &readSet)) handleRead(fd);
            if (!clients.count(fd)) continue;
            if (FD_ISSET(fd, &writeSet)) handleWrite(fd);
        }
    }

    void cleanup() {
        for (auto& [fd, _] : clients) ::close(fd);
        clients.clear();
        backends.clear();
        if (listenFd >= 0) { ::close(listenFd); listenFd = -1; }
    }
};
