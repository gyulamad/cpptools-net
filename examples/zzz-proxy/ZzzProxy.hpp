#pragma once

// =============================================================================
// ZzzProxy.hpp — Sleep/Wake Proxy with Idle Timeout Shutdown Support
// =============================================================================

#include "../../TcpProxy.hpp"
#include "../../../misc/Logger.hpp"
#include "../../../misc/get_time_sec.hpp"
#include "../../../misc/Executor.hpp"
#include "../../../misc/sec_to_datetime.hpp"

#include <thread>

using namespace std;


class ZzzProxy: public TcpProxy {
public:
    ZzzProxy(): TcpProxy() {}
    virtual ~ZzzProxy() {
        if (t.joinable()) t.join();
    }

    void forward(
        int port, const string &host, int hport,
        const string &wakeCmd = "",
        const string &checkCmd = "",
        const string &sleepCmd = "",
        int idleTimeoutSec = 300
    ) {
        this->wakeCmd = wakeCmd;
        this->checkCmd = checkCmd;
        this->sleepCmd = sleepCmd;
        this->idleTimeoutSec = idleTimeoutSec;
        if (idleTimeoutSec < 0) 
            throw ERROR("Idle timeout must be positive or zero");

        LOG("ZzzProxy config:");
        LOG("  Backend: " + host + ":" + to_string(hport));
        LOG("Wake cmd: " + EMPTY_OR(wakeCmd));
        LOG("Sleep cmd: " + EMPTY_OR(sleepCmd));
        LOG("Idle timeout: " + to_string(this->idleTimeoutSec) + " seconds");

        updateActivityTime();
        TcpProxy::forward(port, host, hport);
    }

protected:
    int idleTimeoutSec;
    string wakeCmd;
    string checkCmd;
    string sleepCmd;
    time_t lastActivityTime;
    bool loading = false;
    bool loaded = false;
    thread t;

    void onClientConnect(int fd, const string& addr) override {
        startService();
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
        updateActivityTime();
    }

    void onTick() override {
        TcpProxy::onTick();
        stopService();
    }

    // ----------------

    void startService() {
        if (!loading && !loaded) {
            loading = true;
            if (!wakeCmd.empty()) {
                if (t.joinable()) t.join();           
                t = thread([&](){
                    exec(wakeCmd, true, true);
                });
            }
        }
        while (loading) {
            sleep(10);
            if (checkCmd.empty() || exec(checkCmd, true, false).ret == 0) {
                loading = false;
                loaded = true;
            }
        }
    }

    void stopService() {
        if (!loaded || sleepCmd.empty() || idleTimeoutSec <= 0)
            return;
        if (get_time_sec() < lastActivityTime + idleTimeoutSec)
            return;
        loading = false;
        loaded = false;
        lastActivityTime = 0;
        // if (t.joinable()) t.join();           
        // t = thread([&](){
            exec(sleepCmd, true, true);
            sleep(10);
            // if (t.joinable()) t.join();
        // });
    }

    void updateActivityTime() {
        lastActivityTime = get_time_sec();
        LOG("Activity updated to " + sec_to_datetime(lastActivityTime));
    }
};