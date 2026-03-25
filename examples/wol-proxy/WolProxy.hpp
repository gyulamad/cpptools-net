#pragma once

#include "../../../misc/Executor.hpp"
#include "../../TcpProxy.hpp"
#include "../../wol.hpp"

using namespace std;

class WolProxy: public TcpProxy {
public:
    WolProxy(): TcpProxy() {}
    virtual ~WolProxy() {}

    void forward(int port, const string& host, int hport, const string& wolip, const string& mac, const string& user, const string& cmd) {
        this->wolip = wolip;
        this->mac = mac;
        // this->user = user;
        // this->cmd = cmd;
        this->ssh = cmd.empty() ? "" : "ssh " + user + "@" + host + " 'setsid " + cmd + " >/dev/null 2>&1 < /dev/null &'";
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
        TcpProxy::onClientConnect(fd, addr);
    }

protected:
    string wolip;
    string mac;
    // string user;
    // string cmd;
    string ssh;
};