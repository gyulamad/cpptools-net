#pragma once

// =============================================================================
// ZzzProxyApplication.hpp — Main application wrapper for sleep/wake proxy  
// =============================================================================

#include "../../../misc/App.hpp"
#include "../../../misc/Arguments.hpp"
#include "../../../misc/ConsoleLogger.hpp"
#include "../../../misc/explode.hpp"

#include "ZzzProxy.hpp"

using namespace std;

class ZzzProxyApplication : public App<ConsoleLogger, Arguments> {
public:
    using App::App;
    virtual ~ZzzProxyApplication() {}

    virtual int process() override {
        args.addHelp(1, "port", "Local listening port");
        args.addHelp(2, "backend", "<host>:<port>");

        // New timeout/script parameters unique to zzz-proxy
        args.addHelp("startcmd", "/path/to/start-script.sh OR command");
        args.addHelp("stopcmd", "/path/to/stop-script.sh OR command");
        args.addHelp("timeout", "[default:300] Idle seconds before triggering sleep cmd");

        int port = args.get<int>(1);
        
        vector<string> addr = trim(explode(":", args.get<string>(2)));
        if(addr.size() != 2)
            throw ERROR("Invalid backend format. Use <host>:<port>");
        
        string startcmd = args.getopt<string>("startcmd", "");
        if (startcmd.empty())
            throw ERROR("Start command can not be empty");
        string stopcmd = args.getopt<string>("stopcmd", "");
        if (stopcmd.empty())
            throw ERROR("Stop command can not be empty");
        int timeout = args.getopt<int>("timeout", 300);
        if (timeout <= 0)
            throw ERROR("Timeout can not be less than or equal to zero");

        int backendPort = parse<int>(addr[1]);
        
        // Construct and run proxy instance:
        ZzzProxy* s = new ZzzProxy;
        s->forward(
            port, addr[0], backendPort,
            startcmd, stopcmd, timeout
        );
           
        delete s;
        return 0; 
    }
};
