#pragma once

#ifdef TEST

#include "../cpptools/misc/TEST.hpp"
#include "../cpptools/misc/NullLogger.hpp"
#include "../TcpServer.hpp"
#include <thread>
#include <chrono>

using namespace std;

// =============================================================================
// Integration test: denied IP never reaches onClientConnect (server won't wake)
// =============================================================================
// Proves that TcpServer::acceptClients() drops the fd BEFORE calling
// onClientConnect(), so ZzzProxy::turnServerOn() is never triggered.
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

TEST(test_IpWhitelist_extractIp_simple) {
    IpWhitelist w;
    assert(IpWhitelist::extractIp("192.168.1.1:8080") == "192.168.1.1" && "Should extract IP from addr");
}

TEST(test_IpWhitelist_extractIp_no_port) {
    IpWhitelist w;
    assert(IpWhitelist::extractIp("192.168.1.1") == "192.168.1.1" && "Should return full string if no port");
}

TEST(test_IpWhitelist_disabled_allows_all) {
    IpWhitelist w;
    assert(!w.isEnabled() && "Filtering should be disabled by default");
    assert(w.isAllowed("192.168.1.1:12345") && "Should allow when filtering is disabled");
    assert(w.check("any.ip.here:9999") && "check() should return true when disabled");
}

TEST(test_IpWhitelist_exact_match_allowed) {
    IpWhitelist w;
    w.setPatterns({"^192\\.168\\.1\\.1$"});
    assert(w.isEnabled() && "Filtering should be enabled after setting patterns");
    assert(w.isAllowed("192.168.1.1:8080") && "Exact IP match should be allowed");
}

TEST(test_IpWhitelist_exact_match_denied) {
    IpWhitelist w;
    w.setPatterns({"^192\\.168\\.1\\.1$"});
    assert(!w.isAllowed("10.0.0.5:4321") && "Non-matching IP should be denied");
}

TEST(test_IpWhitelist_regex_range_allowed) {
    IpWhitelist w;
    w.setPatterns({"^192\\.168\\.[0-9]+\\.[0-9]+$"});
    assert(w.isAllowed("192.168.5.10:22") && "Regex range should match 192.168.x.x");
}

TEST(test_IpWhitelist_regex_range_denied) {
    IpWhitelist w;
    w.setPatterns({"^192\\.168\\.[0-9]+\\.[0-9]+$"});
    assert(!w.isAllowed("10.0.0.1:22") && "Regex range should not match 10.x.x.x");
}

TEST(test_IpWhitelist_multiple_patterns) {
    IpWhitelist w;
    w.setPatterns({"^192\\.168\\.[0-9]+\\.[0-9]+$", "^10\\.0\\.0\\.[0-9]+$"});
    assert(w.isAllowed("192.168.1.1:80") && "Should match first pattern");
    assert(w.isAllowed("10.0.0.5:443") && "Should match second pattern");
    assert(!w.isAllowed("172.16.0.1:8080") && "Should deny non-matching IP");
}

TEST(test_IpWhitelist_check_denies_ip) {
    // Suppress LOG output during this test to avoid warning about console output
    Logger* origLogger = logger();
    static NullLogger nullLog;
    logger(&nullLog);
    
    IpWhitelist w;
    w.setPatterns({"^127\\.0\\.0\\.1$"});
    assert(!w.check("192.168.1.1:1234") && "check() should return false for denied IP");
    
    logger(origLogger);
}

TEST(test_IpWhitelist_check_allows_matching_ip) {
    IpWhitelist w;
    w.setPatterns({"^127\\.0\\.0\\.1$"});
    assert(w.check("127.0.0.1:80") && "check() should return true for allowed IP");
}

TEST(test_IpWhitelist_empty_patterns_disables_filtering) {
    IpWhitelist w;
    w.setPatterns({});
    assert(!w.isEnabled() && "Empty patterns should disable filtering");
    assert(w.isAllowed("any.ip:99") && "Should allow all when no patterns set");
}

#endif // TEST
