#pragma once

// =============================================================================
// ZzzProxy.hpp — Sleep/Wake Proxy with Idle Timeout Shutdown Support
// =============================================================================

#include "../../TcpProxy.hpp"
#include "../../../misc/Logger.hpp"
#include "../../../misc/get_time_sec.hpp"
#include "../../../misc/Executor.hpp"
#include "../../../misc/sec_to_datetime.hpp"
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
    time_sec lastActivityTime = 0; // 0 - means server turned off

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

    void onRawData(int clientFd, string& buf) override {
        TcpProxy::onRawData(clientFd, buf);
        // updateActivityTime();
    }

    void onTick() override {
        if (isServerOn() && isIdleTimeouts())
            turnServerOff();
        TcpProxy::onTick();
    }

    // ----------------

    void turnServerOn() {
        // TODO
        LOG("Start server...");
        exec_result_t res = exec(startCmd, true, false);
        cout << res.out << endl;
        cerr << res.err << endl;
        if (res.ret) throw ERROR("Execution failed: " + to_string(res.ret));
        updateActivityTime();
    }

    void turnServerOff() {
        LOG("Stop server...");
        exec_result_t res = exec(stopCmd, true, false);
        cout << res.out << endl;
        cerr << res.err << endl;
        if (res.ret) throw ERROR("Execution failed: " + to_string(res.ret));
        lastActivityTime = 0;
    }

    bool isServerOn() {
        return lastActivityTime;
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
        lastActivityTime = now;
        LOG("Last activity updated to " + sec_to_datetime(lastActivityTime));
    }
};