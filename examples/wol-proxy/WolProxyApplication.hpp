#pragma once

#include "../../../misc/App.hpp"
#include "../../../misc/Arguments.hpp"
#include "../../../misc/ConsoleLogger.hpp"
#include "../../../misc/explode.hpp"

#include "WolProxy.hpp"

using namespace std;

class WolProxyApplication: public App<ConsoleLogger, Arguments> {
public:
    using App::App;
    virtual ~WolProxyApplication() {}

    virtual int process() override {
        args.addHelp(1, "port", "Listening port number");
        args.addHelp(2, "address", "Server <address>:<port> for forwarding");
        args.addHelp(3, "wolip", "WOL broadcast IP address");
        args.addHelp(4, "mac", "Server MAC addres for WOL");
        args.addHelp(5, "user", "SSH user who runs startup command on WOL");
        args.addHelp(6, "cmd", "SSH command to run when server wakes up");
        int port = args.get<int>(1);
        vector<string> addr = trim(explode(":", args.get<string>(2)));
        if (addr.size() != 2)
            throw ERROR("Invalid address format. Use <host>:<port>");
        string wolip = args.get<string>(3);
        string mac = args.get<string>(4);
        string user = args.getopt<string>(5, "");
        string cmd = args.getopt<string>(6, "");
        if ((!user.empty() && cmd.empty()) ||   // XNOR
            (user.empty() && !cmd.empty()))
            throw ERROR("User and command parameter both should be defined when one of them defined.");
        WolProxy s;
        s.forward(port, addr[0], parse<int>(addr[1]), wolip, mac, user, cmd);
        return 0;
    }
};