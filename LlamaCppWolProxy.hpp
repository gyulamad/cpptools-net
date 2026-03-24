#pragma once

// DEPENDENCY: nlohmann/json:v3.12.0

#include "WolProxy.hpp"
#include "wol_ready.hpp"

// =============================================================================
// LlamaCppWolProxy - WolProxy with HTTP health check for llama.cpp model loading
// =============================================================================


class LlamaCppWolProxy: public WolProxy {
public:
    LlamaCppWolProxy() : WolProxy() {}
    virtual ~LlamaCppWolProxy() {}

    void forward(int port, const string& host, int hport, const string& wolip, const string& mac, const string& user, const string& cmd, int healthRetry, int healthDelay) {
        this->wolip = wolip;
        this->mac = mac;
        // this->user = user;
        // this->cmd = cmd;
        this->ssh = cmd.empty() ? "" : "ssh " + user + "@" + host + " 'setsid " + cmd + " >/dev/null 2>&1 < /dev/null &'";
        this->healthRetry = healthRetry;
        this->healthDelay = healthDelay;
        TcpProxy::forward(port, host, hport);
    }

    void onClientConnect(int fd, const string& addr) override {
        LOG("Client connected, ping to see the server state...");
        bool p = ping(backendHost);
        LOG("Server is " + (p ? "up" : "down, wake on lan..."));
        if (!p && wol(wolip, mac, backendHost) && !ssh.empty()) {
            LOG("Server is up now");
            if (!p) {
                LOG("SSH: " + ssh);
                exec(ssh, true);
            }
            // this->ssh = "";
        }
        // Check HTTP health for llama.cpp model loading
        LOG("Checking llama.cpp /health endpoint...");
        if (!wol_ready(backendHost, backendPort, healthRetry, healthDelay)) {
            LOG("ERROR: Server is up but model failed to load or timeout");
            disconnectClient(fd);
            return;
        }
        LOG("Model is ready");
        TcpProxy::onClientConnect(fd, addr);
    }

protected:
    int healthRetry = 30;
    int healthDelay = 2000;
};
