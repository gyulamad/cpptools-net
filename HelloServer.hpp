#pragma once

#include "HttpServer.hpp"
#include <iostream>
#include <deque>
#include <unordered_map>
#include <chrono>

using namespace std;

// =============================================================================
// HelloServer
//
//   A simple HTTP server that serves a "Hello World" page slowly,
//   outputting content character by character to demonstrate concurrent
//   client handling.
// =============================================================================
class HelloServer : public HttpServer {
public:
    using HttpServer::HttpServer;
    virtual ~HelloServer() {}

protected:
    void onServerStart(uint16_t port) override {
        cout << "[HelloServer] Listening on http://localhost:" << port << "/" << endl;
    }

    void onServerStop() override {
        cout << "[HelloServer] Stopped." << endl;
    }

    void onHttpClientConnect(int fd, const string& addr) override {
        cout << "[+] Client " << fd << " connected from " << addr << endl;
    }

    void onHttpClientDisconnect(int fd) override {
        cout << "[-] Client " << fd << " disconnected." << endl;
        dripQueues.erase(fd);
    }

    void onHttpRequest(int fd, const HttpRequest& req) override {
        cout << "[" << fd << "] " << req.method << " " << req.path << endl;

        if (req.method != "GET") {
            sendHttpResponse(fd, HttpResponse::methodNotAllowed());
            closeAfterFlush(fd);
            return;
        }

        // Queue the full response for drip-feeding
        string response = HttpResponse::html(getHelloPage()).serialize();
        auto& queue = dripQueues[fd];
        for (char c : response)
            queue.pending.push_back(c);
        queue.lastSend = chrono::steady_clock::now();
    }

    // Drip one character per tick (~100ms) per client
    void onTick() override {
        auto now = chrono::steady_clock::now();
        for (auto& [fd, state] : dripQueues) {
            if (state.pending.empty()) continue;
            
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                now - state.lastSend).count();
            if (elapsed < 10) continue;  // some delay for testing
            
            string nxt = "";
            while (true) {
                char c = state.pending.front();
                state.pending.pop_front();
                nxt += c;
                if (c == ' ') break;
                if (state.pending.empty()) break;
            }
            
            state.lastSend = now;
            
            cout << nxt << flush;
            sendToClient(fd, nxt);
            
            // Close connection after sending last character
            if (state.pending.empty())
                closeAfterFlush(fd);
        }
    }

private:
    struct DripState {
        deque<char> pending;
        chrono::steady_clock::time_point lastSend = chrono::steady_clock::now();
    };

    unordered_map<int, DripState> dripQueues;

    static string getHelloPage() {
        return R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Hello World</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            max-width: 800px;
            margin: 0 auto;
            padding: 40px 20px;
            line-height: 1.6;
            color: #333;
        }
        h1 {
            color: #2c3e50;
            border-bottom: 2px solid #3498db;
            padding-bottom: 10px;
        }
        p {
            margin: 1em 0;
        }
        .lorem {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 8px;
            border-left: 4px solid #3498db;
        }
    </style>
</head>
<body>
    <h1>Hello World</h1>
    <p>Welcome to this simple HTTP server demonstration!</p>
    <div class="lorem">
        <p><strong>Lorem ipsum</strong> dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur.</p>
        <p>Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum. Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo.</p>
        <p>Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum quia dolor sit amet, consectetur, adipisci velit, sed quia non numquam eius modi tempora incidunt ut labore et dolore magnam aliquam quaerat voluptatem.</p>
    </div>
    <p><em>This page was served by HelloServer on port 8081.</em></p>
</body>
</html>)";
    }
};
