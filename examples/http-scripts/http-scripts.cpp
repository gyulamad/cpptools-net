
#include "../../HttpServer.hpp"

#include "../../../misc/App.hpp"

#include <functional>
#include "../../../misc/Arguments.hpp"
#include "../../../misc/ConsoleLogger.hpp"

// Type alias for route handler functions
using RouteHandler = function<void(int fd)>;

class HttpScriptsServer : public HttpServer {
public:
    using HttpServer::HttpServer;
    virtual ~HttpScriptsServer() {}

protected:
    void onHttpRequest(int fd, const HttpRequest& req) override {
        cout << "[" << fd << "] " << req.method << " " << req.path << endl;

        // Look up the route in our routing table
        auto key = makeRouteKey(req.method, req.path);
        auto it = routes.find(key);

        if (it == routes.end()) {
            // No matching route found — return 404 Not Found
            sendHttpResponse(fd, HttpResponse::notFound("Not Found"));
            return;
        }

        // Execute the matched route handler
        it->second(fd);
    }

private:
    // Helper: combine method + path into single lookup key
    static string makeRouteKey(const string& method, const string& path) {
        return method + ":" + path;
    }

    // Routing table: stores all registered routes (method:path → handler)
    unordered_map<string, RouteHandler> routes = {
        {"GET:/hello/(\\d+)", [this](int fd) { handleGetHello(fd, args); }}
    };

    // ========= Controllers ======================================================

    void handleGetHello(int fd) {
        HttpResponse res = HttpResponse::ok("Hello, World!");
        sendHttpResponse(fd, res);
    }
};


class HttpScriptsApplication : public App<ConsoleLogger, Arguments> {
public:
    using App::App;
    virtual ~HttpScriptsApplication() {}

    virtual int process() override {
        args.addHelp(1, "port", "Listening port number");
        int port = args.get<int>(1);
        HttpScriptsServer server;        
        server.listen(port);
        return 0;
    }
};

int main(int argc, char* argv[]) {
    return HttpScriptsApplication(argc, argv);
}
