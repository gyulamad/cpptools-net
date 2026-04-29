#define TEST
#define TEST_SHOW_DETAILS

#include "../../../misc/TEST.hpp"
#include "../../IpWhitelist.hpp"
#include "../../TcpServer.hpp"  // For integration test — no circular dep here

int main(int argc, char** argv) {
    createLogger<ConsoleLogger>();
    Arguments args(argc, argv);
    tester.run(args);
}

// =============================================================================
// Integration test: denied IP never reaches onClientConnect (server won't wake)
// =============================================================================
TEST(test_IpWhitelist_denied_ip_never_reaches_on_client_connect) {
    Logger* origLogger = logger();
    static NullLogger nullLog;
    logger(&nullLog);

    // Subclass TcpServer that tracks whether onClientConnect was called
    class TrackingTcpServer : public TcpServer {
    public:
        bool connectCalled = false;
        
        void onRawData(int /*fd*/, string& /*buf*/) override {}  // no-op
        
        void onClientConnect(int /*fd*/, const string& /*addr*/) override {
            connectCalled = true;  // This should NEVER be set for denied IPs
        }
    };

    TrackingTcpServer server;
    
    // Deny ALL traffic including localhost (127.0.0.1)
    IpWhitelist wl;
    wl.setPatterns({"^999\\.999\\.999\\.999$"});  // No real IP matches this
    server.setWhitelist(wl);

    // Start the server on a high port in a separate thread
    std::thread srvThread([&]() {
        try {
            server.listen(17923);
        } catch (...) {
            // Server stopped or error — expected when we call stop()
        }
    });

    // Wait for the listen socket to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Connect a client from localhost (which is DENIED by our whitelist)
    try {
        int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(17923);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        
        if (::connect(cfd, (sockaddr*)&addr, sizeof(addr)) >= 0) {
            // Connection accepted at kernel level — now the server should close it
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            ::close(cfd);
        } else {
            // connect() failed (RST received) — also valid proof of rejection
            ::close(cfd);
        }
    } catch (...) {
        // Connection rejected — expected behavior for denied IP
    }

    // Give the server time to process the accept/close cycle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop the server cleanly
    server.stop();
    if (srvThread.joinable()) srvThread.join();

    logger(origLogger);

    assert(!server.connectCalled && 
        "onClientConnect must NOT be called for denied IPs — backend would never wake up");
}
