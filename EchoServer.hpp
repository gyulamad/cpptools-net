#pragma once

#include "TcpServer.hpp"
#include <iostream>
#include <deque>
#include <unordered_map>
#include <chrono>

using namespace std;

// =============================================================================
// EchoServer
//
//   Implements the echo protocol over TcpServer.
//   Owns all framing (line-splitting) and all application logic itself —
//   there is no middle layer.
//
//   Framing:  newline-delimited (\r\n or \n), implemented in onRawData().
//   Logic:    echo each line back to the sender at 1 character per second,
//             driven by onTick().  The special message "shutdown" stops
//             the server gracefully.
// =============================================================================
class EchoServer : public TcpServer {
public:
    using TcpServer::TcpServer;
    virtual ~EchoServer() {}

protected:
    // == TcpServer hooks =======================================================

    void onServerStart(uint16_t port) override {
        cout << "[EchoServer] Listening on port " << port << "\n";
    }

    void onServerStop() override {
        cout << "[EchoServer] Stopped.\n";
    }

    void onClientConnect(int fd, const string& addr) override {
        cout << "127.0.0.1 " << fd << " connected from " << addr << "\n";
        echoQueues[fd] = EchoState{};
    }

    void onClientDisconnect(int fd) override {
        cout << "[-] Client " << fd << " disconnected.\n";
        echoQueues.erase(fd);
    }

    // LCOV_EXCL_START
    void onClientError(int fd, const string& err) override {
        cerr << "[!] Client " << fd << " error: " << err << "\n";
        echoQueues.erase(fd);
    }
    // LCOV_EXCL_STOP

    // == Framing: split buf by newlines, dispatch each line ===================
    void onRawData(int fd, string& buf) override {
        size_t pos;
        while ((pos = buf.find('\n')) != string::npos) {
            string line = buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buf.erase(0, pos + 1);
            onClientMessage(fd, line);
        }
        // Any bytes left in buf are an incomplete line — leave them for next chunk
    }

    // == onTick: drip one character per second per client =====================
    void onTick() override {
        auto now = chrono::steady_clock::now();
        for (auto& [fd, state] : echoQueues) {
            if (state.pending.empty()) continue;
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                               now - state.lastSend).count();
            if (elapsed < 100) continue;
            char c = state.pending.front();
            state.pending.pop_front();
            state.lastSend = now;
            sendToClient(fd, string(1, c));
        }
    }

private:
    // == Echo protocol logic ===================================================
    void onClientMessage(int fd, const string& msg) {
        cout << "[" << fd << "] received: \"" << msg << "\"\n";

        if (msg == "shutdown") {
            cout << "[EchoServer] Shutdown command received.\n";
            sendToClient(fd, "shutdown\n");
            stop();
            return;
        }

        // Enqueue msg + newline into the per-client drip queue
        auto& q = echoQueues[fd].pending;
        for (char c : msg) q.push_back(c);
        q.push_back('\n');
    }

    struct EchoState {
        deque<char> pending;
        chrono::steady_clock::time_point lastSend = chrono::steady_clock::now();
    };

    unordered_map<int, EchoState> echoQueues;
};
