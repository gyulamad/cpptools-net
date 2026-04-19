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
        
        // Optional WOL params (for backwards compatibility) 
        args.addHelp(3, "wolip", "[optional] WOL broadcast IP");   
        args.addHelp(4, "mac",   "[optional] MAC address");

        // New timeout/script parameters unique to zzz-proxy
        args.addHelp(5, "wakecmd", "/path/to/wake-script.sh OR command");
        args.addHelp(6, "sleepcmd", "/path/to/stop-script.sh OR command");
        args.addHelp(7, "timeout", "[default:300] Idle seconds before triggering sleep cmd");

        int port = args.get<int>(1);
        
        vector<string> addr = trim(explode(":", args.get<string>(2)));
        if(addr.size() != 2)
            throw ERROR("Invalid backend format. Use <host>:<port>");
            
        string wolip = args.getopt<string>(3, "");
        string mac   = args.getopt<string>(4, "");
        
        string wakecmd = args.getopt<string>(5, "./wake.sh");
        string sleepcmd= args.getopt<string>(6, "./sleep.sh");
        int idleTimeoutSec = args.getopt<int>(7, 300);

        int backendPort = parse<int>(addr[1]);
        
        // Construct and run proxy instance:
        ZzzProxy s;
        if (!wolip.empty() && !mac.empty()) {
            // Has WOL parameters (backward compatibility)
            s.forward(port, addr[0], backendPort,
                      wolip, mac,
                      wakecmd, sleepcmd, idleTimeoutSec);
        } else {
            // No WOL support needed  
            s.forward(port, addr[0], backendPort,
                      "", "",
                      wakecmd, sleepcmd, idleTimeoutSec);
        }
                  
        return 0; 
    }
};
