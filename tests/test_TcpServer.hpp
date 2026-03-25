#pragma once

#ifdef TEST

#include "../cpptools/misc/TEST.hpp"
#include "../cpptools/misc/capture_cout.hpp"
#include <thread>
#include "../EchoServer.hpp"
#include "../TcpClientB.hpp"

using namespace std;

TEST(test_TcpServer_echo_single_client) {
    capture_cout([]() {
        EchoServer s;
        thread st([&s]() { s.listen(9090); });
        TcpClientB c;
        c.connect("localhost", 9090);
        c.send("Hello!");
        while (!c.available()); // waiting for response...
        const string echo = c.read();
        c.send("shutdown"); // test tcp echo server must implement a shutdown command so that tests won't stuck...
        if (st.joinable()) st.join();
        assert(echo == "Hello!" && "Echo must be the same as original message");
    });
}

TEST(test_TcpServer_echo_multi_client) {
    capture_cout([]() {
        EchoServer s;
        thread st([&s]() { s.listen(9090); });

        TcpClientB c1, c2, c3;
        string echo1, echo2, echo3;
        double elapsed1, elapsed2, elapsed3;

        // LCOV_EXCL_START
        thread c1t([&]() {
        // LCOV_EXCL_STOP
            Stopper stopper;
            c1.connect("localhost", 9090);
            c1.send("Hello1");
            while (!c1.available()); // waiting for response...
            echo1 = c1.read();
            elapsed1 = stopper.stop();            
        });
        // LCOV_EXCL_START
        thread c2t([&]() {
        // LCOV_EXCL_STOP
            Stopper stopper;
            c2.connect("localhost", 9090);
            c2.send("Hello2");
            while (!c2.available()); // waiting for response...
            echo2 = c2.read();
            elapsed2 = stopper.stop();
        });
        // LCOV_EXCL_START
        thread c3t([&]() {
        // LCOV_EXCL_STOP
            Stopper stopper;
            c3.connect("localhost", 9090);
            c3.send("Hello3");
            while (!c3.available()); // waiting for response...
            echo3 = c3.read();
            elapsed3 = stopper.stop();
        });
        
        if (c1t.joinable()) c1t.join();
        if (c2t.joinable()) c2t.join();
        if (c3t.joinable()) c3t.join();
        
        c1.send("shutdown"); // test tcp echo server must implement a shutdown command so that tests won't stuck...
        if (st.joinable()) st.join();
        
        assert(echo1 == "Hello1" && "Echo must be the same as original message");
        assert(echo2 == "Hello2" && "Echo must be the same as original message");
        assert(echo3 == "Hello3" && "Echo must be the same as original message");
        assert(abs(elapsed1 - elapsed2) < 100 && "Everything must happend in the same time");
        assert(abs(elapsed1 - elapsed3) < 100 && "Everything must happend in the same time");
    });
}

// -----------------------------------------------------------------------------
// Test: TcpServer::closeAfterFlush() with empty sendQueue
// Coverage: Line 101-102 in TcpServer.hpp - "if (it->second.sendQueue.empty())"
// -----------------------------------------------------------------------------
TEST(test_TcpServer_closeAfterFlush_empty_queue) {
    // Create a custom test server that exposes closeAfterFlush behavior
    class TestServer : public TcpServer {
    public:
        void testCloseAfterFlush(int fd) {
            closeAfterFlush(fd);
        }
        
        bool hasClient(int fd) {
            return clients.find(fd) != clients.end();
        }
        
        void addTestClient(int fd) {
            clients[fd] = ClientState{"127.0.0.1:1234", {}, {}, false};
        }
        
        void removeTestClient(int fd) {
            auto it = clients.find(fd);
            if (it != clients.end()) {
                ::close(fd);
                clients.erase(it);
            }
        }
        
    // LCOV_EXCL_START
    private:
        void onRawData(int /*fd*/, string& /*buf*/) override {}
    };
    // LCOV_EXCL_STOP
    
    TestServer server;
    
    // Create a socket pair to test with
    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    assert(rc == 0 && "socketpair should succeed");
    
    // Add a client with empty sendQueue
    server.addTestClient(sv[0]);
    
    // closeAfterFlush should disconnect immediately (queue is empty)
    server.testCloseAfterFlush(sv[0]);
    
    // Client should be removed
    bool removed = !server.hasClient(sv[0]);
    
    close(sv[1]);
    
    assert(removed && "Client should be disconnected when sendQueue is empty");
}

// -----------------------------------------------------------------------------
// Test: TcpServer::closeAfterFlush() with non-empty sendQueue
// Coverage: Line 103-104 in TcpServer.hpp - "else it->second.pendingClose = true;"
// -----------------------------------------------------------------------------
TEST(test_TcpServer_closeAfterFlush_pending_data) {
    class TestServer : public TcpServer {
    public:
        void testCloseAfterFlush(int fd) {
            closeAfterFlush(fd);
        }
        
        bool hasClient(int fd) {
            return clients.find(fd) != clients.end();
        }
        
        bool isPendingClose(int fd) {
            auto it = clients.find(fd);
            return it != clients.end() && it->second.pendingClose;
        }
        
        void addTestClientWithData(int fd) {
            clients[fd] = ClientState{"127.0.0.1:1234", {}, {'d', 'a', 't', 'a'}, false};
        }
        
        void removeTestClient(int fd) {
            auto it = clients.find(fd);
            if (it != clients.end()) {
                ::close(fd);
                clients.erase(it);
            }
        }
        
    // LCOV_EXCL_START
    private:
        void onRawData(int /*fd*/, string& /*buf*/) override {}
    };
    // LCOV_EXCL_STOP
    
    TestServer server;
    
    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    assert(rc == 0 && "socketpair should succeed");
    
    // Add a client with data in sendQueue
    server.addTestClientWithData(sv[0]);
    
    // closeAfterFlush should set pendingClose flag (not disconnect yet)
    server.testCloseAfterFlush(sv[0]);
    
    // Client should still exist with pendingClose flag
    bool exists = server.hasClient(sv[0]);
    bool pending = server.isPendingClose(sv[0]);
    
    server.removeTestClient(sv[0]);
    close(sv[1]);
    
    assert(exists && "Client should still exist");
    assert(pending && "pendingClose flag should be set");
}

#endif