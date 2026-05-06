#pragma once

// =============================================================================
// ZzzProxy.hpp - Sleep/Wake Proxy with Idle Timeout Shutdown Support
// =============================================================================

#include "../../TcpProxy.hpp"
#include "../../../misc/Logger.hpp"
#include "../../../misc/get_time_sec.hpp"
#include "../../../misc/Executor.hpp"
#include "../../../misc/sec_to_datetime.hpp"
#include <atomic>
#include <string>

using namespace std;


class ZzzProxy: public TcpProxy {
public:
    ZzzProxy(): TcpProxy() {}
    virtual ~ZzzProxy() {}

    void forward(
        int port, const string &host, int hport,
        const string &startCmd = "",
        const string &stopCmd = "",
        int idleTimeoutSec = 300
    ) {
        this->startCmd = startCmd;
        this->stopCmd = stopCmd;
        this->idleTimeoutSec = idleTimeoutSec;

        LOG("ZzzProxy config:");
        LOG("Backend .........: " + host + ":" + to_string(hport));
        LOG("Start command ...: " + EMPTY_OR(startCmd));
        LOG("Stop command ....: " + EMPTY_OR(stopCmd));
        LOG("Idle timeout ....: " + to_string(this->idleTimeoutSec) + " seconds");

        TcpProxy::forward(port, host, hport);
    }

protected:
     int idleTimeoutSec = 0; // 0 - means never timeouts
    string startCmd;
    string stopCmd;
    time_sec lastActivityTime = 0;   // 0 -> server off (turned off by proxy via idle timeout)

    // FIX for stale state bug: tracks whether a backend failure was detected.
    // Even if lastActivityTime > 0, isServerOn() returns false when this flag is set.
    atomic<bool> backendFailureDetected{false};

    void onClientConnect(int fd, const string& addr) override {
        if (!isServerOn())
            turnServerOn();
        TcpProxy::onClientConnect(fd, addr);
        updateActivityTime();
    }

    void onClientDisconnect(int fd) override {
        TcpProxy::onClientDisconnect(fd);
        updateActivityTime();
    }

    void onClientError(int fd, const string& err) override {
        TcpProxy::onClientError(fd, err);
        updateActivityTime();
    }

    // FIX: Mark backend failure so isServerOn() returns false (clears stale state).
    // Called BEFORE disconnectClient(), which would otherwise re-set lastActivityTime.
    void onBackendConnectionFailed(int /*clientFd*/) override {
        LOG("[!] Backend connection failed - marking server as OFF");
        backendFailureDetected.store(true);
    }

    // FIX: Mark unexpected backend disconnection (crash, network drop).
    void onBackendDisconnected(int /*clientFd*/) override {
        LOG("[-] Backend disconnected unexpectedly - marking server as OFF");
        backendFailureDetected.store(true);
    }

    void onRawData(int clientFd, string& buf) override {
        TcpProxy::onRawData(clientFd, buf);
        updateActivityTime();
    }

    void onTick() override {
        if (isServerOn() && isIdleTimeouts())
            turnServerOff();
        TcpProxy::onTick();
    }

    // ----------------

    void turnServerOn() {
        LOG("Start server...");
        if (!startCmd.empty()) {
            exec_result_t res = exec(startCmd, true, false);
            cout << res.out << endl;
            cerr << res.err << endl;
            if (res.ret) throw ERROR("Execution failed: " + to_string(res.ret));
        } else {
            LOG("[info] No start command configured - assuming server is managed externally");
        }
        // Clear failure flag - we just started the server successfully
        backendFailureDetected.store(false);
        updateActivityTime();
    }

    void turnServerOff() {
        LOG("Stop server...");
        exec_result_t res = exec(stopCmd, true, false);
        cout << res.out << endl;
        cerr << res.err << endl;
        if (res.ret) throw ERROR("Execution failed: " + to_string(res.ret));
        // Intentional shutdown - clear failure flag and reset activity time
        backendFailureDetected.store(false);
        lastActivityTime = 0;
    }

    bool isServerOn() {
        return !backendFailureDetected.load() && lastActivityTime != 0;
    }

    bool isIdleTimeouts() {
        return 
            idleTimeoutSec &&
            isServerOn() && 
            get_time_sec() - lastActivityTime > idleTimeoutSec;
    }

    void updateActivityTime() {
        time_sec now = get_time_sec();
        if (now == lastActivityTime)
            return;
        // Only update activity time when no backend failure is pending.
        // If a failure was detected, the server should be considered OFF until
        // turnServerOn() succeeds and clears the flag.
        if (!backendFailureDetected.load()) {
            lastActivityTime = now;
            LOG("Last activity updated to " + sec_to_datetime(lastActivityTime));
        }
    }
};

