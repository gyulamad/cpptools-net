#pragma once

#include "../misc/App.hpp"
#include "../misc/Arguments.hpp"
#include "../misc/ConsoleLogger.hpp"

#include "HelloServer.hpp"

using namespace std;

class HelloApplication: public App<ConsoleLogger, Arguments> {
public:
    using App::App;
    virtual ~HelloApplication() {}

    virtual int process() override {
        args.addHelp(1, "port", "Listening port number");
        int port = args.get<int>(1);
        HelloServer server;        
        server.listen(port);
        return 0;
    }
};