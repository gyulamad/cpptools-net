#pragma once

#include "../misc/tpl_replace.hpp"
#include "../misc/Executor.hpp"
#include "ping.hpp"
#include <string>

using namespace std;

// returns true when woke up, false if already was awaik
inline bool wol(
    const string& wolip, // e.g: "255.255.255.255",
    const string& mac, 
    const string& addr, 
    const string& cmd_ping = "ping -c 1 -W 1 {{addr}}", 
    const string& cmd_wol = "wakeonlan -i {{wolip}} {{mac}}",
    int retry = 20,
    function<void(int)> callback = [](int retry) {
        cout << "[WOL] retry " << retry << "..." << endl;
    }
) {
    bool first = true;
    while (!ping(addr, cmd_ping)) {
        if (first)
            first = false;
        else 
            callback(retry);
        string wol = tpl_replace({
            { "{{mac}}", mac },
            { "{{wolip}}", wolip },
        }, cmd_wol);
        exec(wol, true);
        sleep(5);
        retry--;
        if (retry < 0)
            return false;
    }
    return true;
}