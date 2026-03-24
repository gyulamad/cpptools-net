#pragma once

#include <string>
#include "../misc/tpl_replace.hpp"
#include "../misc/Executor.hpp"

using namespace std;

inline bool ping(const string& addr, const string& cmd_ping = "ping -c 1 -W 1 {{addr}}") {
    return !Executor::execute(tpl_replace("{{addr}}", addr, cmd_ping), nullptr, nullptr, false);
}