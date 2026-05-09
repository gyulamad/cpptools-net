#pragma once

#ifdef TEST

#include "../../misc/TEST.hpp"
#include "../../misc/capture_cout_cerr.hpp"
#include <thread>
#include <chrono>
#include "../examples/zzz-proxy/ZzzProxy.hpp"
#include "../examples/echo/EchoServer.hpp"
#include "../TcpClientB.hpp"
#include "TestTcpUtils.hpp"

using namespace std;

// =============================================================================
// ZzzProxy Tests - covers sleep/wake proxy state tracking bugs
// Port naming: backend=51XX, proxy=61XX
// =============================================================================

struct TestableZzzProxy : public ZzzProxy {
    atomic<bool> serverWasOn{false};  // snapshot taken inside onTick (same thread)

    void onTick() override {
        // Capture isServerOn state from within the event-loop thread (thread-safe)
        serverWasOn.store(isServerOn());
        ZzzProxy::onTick();
    }
};


// -----------------------------------------------------------------------------
// Test: After backend dies, proxy must clear stale state so next client can recover.
// Before fix: lastActivityTime stayed > 0 forever after unexpected crash -> stuck.
// After fix: onBackendConnectionFailed sets the failure flag, isServerOn() returns false.
// This test MUST FAIL before fix and PASS after.
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_clears_stale_state_on_backend_crash) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        // Phase 1: Start backend + ZzzProxy (no start/stop cmds, manual control).
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5103); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy; // startCmd="" -> turnServerOn is no-op
        thread proxyThread([&proxy]() { try { proxy.forward(6103, "localhost", 5103); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // First client connects -> sets activity time > 0. Proxy thinks server is ON.
        TcpClientB c1;
        try { c1.connect("localhost", 6103); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(400));

        assert(proxy.serverWasOn.load() == true && "After connect proxy should think server is ON");

        // Phase 2: Kill the backend unexpectedly (simulating crash).
        TcpClientB crasher;
        try {
            crasher.connect("localhost", 5103);
            crasher.send("shutdown");
        } catch (...) {}
        if (backendThread.joinable()) backendThread.join();
        this_thread::sleep_for(chrono::milliseconds(400));

        // Phase 3: New client connects -> proxy tries backend, fails. 
        // FIX: onBackendConnectionFailed should clear stale state via failure flag.
        TcpClientB c2;
        try { c2.connect("localhost", 6103); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(800));

        // After backend failure was detected, proxy must have cleared stale state.
        bool nowOff = (proxy.serverWasOn.load() == false);
        assert(nowOff && "BUG: Proxy still thinks server is ON after backend crash - stale state not cleared");

        // Cleanup
        c1.disconnect();
        c2.disconnect();
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
    });
}


// -----------------------------------------------------------------------------
// Test: Full recovery cycle. Backend crashes, new EchoServer starts on same port.
// Next client should be able to communicate through the proxy successfully.
// Before fix: stale state prevents reconnection -> all clients fail forever.
// After fix: stale state cleared via failure flag -> next connection works when backend is back.
// This test MUST FAIL before fix and PASS after.
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_full_recovery_after_backend_crash) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        // Phase 1: Start first EchoServer + ZzzProxy (no startCmd/stopCmd).
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5104); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy; // no cmds -> manual server control, turnServerOn is no-op
        thread proxyThread([&proxy]() { try { proxy.forward(6104, "localhost", 5104); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(300));

        // Client connects -> sets activity time > 0. Proxy thinks server is ON.
        TcpClientB c1;
        try { c1.connect("localhost", 6104); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(500));

        assert(proxy.serverWasOn.load() == true && "Proxy thinks server is ON");

        // Phase 2: Kill the backend.
        TcpClientB crasher;
        try {
            crasher.connect("localhost", 5104);
            crasher.send("shutdown");
        } catch (...) {}
        if (backendThread.joinable()) backendThread.join();
        this_thread::sleep_for(chrono::milliseconds(500));

        // Phase 3: Start a new EchoServer on same port (simulating server restart).
        EchoServer backend2;
        thread backendThread2([&backend2]() { try { backend2.listen(5104); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(300));

        // Phase 4: New client connects through proxy. 
        // Since stale state was cleared (failure flag), onClientConnect sees isServerOn()=false,
        // calls turnServerOn() which is a no-op (empty startCmd), then proceeds to connect backend -> should succeed now.
        TcpClientB c2;
        bool gotConnection = false;
        try {
            c2.connect("localhost", 6104);
            this_thread::sleep_for(chrono::milliseconds(500));

            // Send a message through the proxy to verify data flows end-to-end.
            c2.send("test");

            // EchoServer drips at ~1 char per second for "test" (4 chars + newline) -> wait up to 3s.
            if (TestTcpUtils::wait_for_available(c2, 5000)) {
                string resp;
                try { resp = c2.read(); } catch (...) {}
                gotConnection = !resp.empty() && resp.find("test") != string::npos;
            }
        } catch (...) {}

        assert(gotConnection && "BUG: Proxy did NOT recover after backend crash+restart");

        // Cleanup - stop proxy first, then join threads, then shutdown echo server.
        c2.disconnect();
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
        this_thread::sleep_for(chrono::milliseconds(300));

        TcpClientB shutdown2;
        try {
            shutdown2.connect("localhost", 5104);
            shutdown2.send("shutdown");
        } catch (...) {}
        if (backendThread2.joinable()) backendThread2.join();
    });
}


// -----------------------------------------------------------------------------
// Test: Backend socket closure must NOT mark server as OFF (the multi-restart bug).
// Before fix: onBackendDisconnected set backendFailureDetected → isServerOn()=false
//             → every new client triggered turnServerOn() → duplicate servers.
// After fix:  onBackendDisconnected only logs; failure flag untouched.
//             Next client reconnects to existing server without restart.
// This test MUST FAIL before fix and PASS after.
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_backend_close_does_not_mark_server_off) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        // Start backend + ZzzProxy (no startCmd/stopCmd).
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5105); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy; // no cmds -> turnServerOn is a no-op
        thread proxyThread([&proxy]() { try { proxy.forward(6105, "localhost", 5105); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // Phase 1: Client connects -> server marked ON.
        TcpClientB c1;
        try { c1.connect("localhost", 6105); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(400));
        assert(proxy.serverWasOn.load() == true && "Proxy should think server is ON after connect");

        // Phase 2: Disconnect client (backend socket closes normally).
        c1.disconnect();
        this_thread::sleep_for(chrono::milliseconds(500));

        // FIX CHECK: Server must STILL be considered ON — backend close ≠ crash.
        assert(proxy.serverWasOn.load() == true && "BUG: Normal disconnect marked server as OFF (multi-restart bug!)");

        // Phase 3: New client connects -> should NOT trigger turnServerOn().
        TcpClientB c2;
        try {
            c2.connect("localhost", 6105);
            this_thread::sleep_for(chrono::milliseconds(500));

            // Verify data flows — if server was restarted unnecessarily, connection might still work
            // but the key assertion above proves state wasn't corrupted.
            c2.send("hello");
            bool gotResp = false;
            if (TestTcpUtils::wait_for_available(c2, 3000)) {
                string resp;
                try { resp = c2.read(); } catch (...) {}
                gotResp = !resp.empty() && resp.find("hello") != string::npos;
            }
            assert(gotResp && "Data should flow through proxy after reconnection");

            // Server state must still be ON.
            assert(proxy.serverWasOn.load() == true && "Server should remain ON throughout test");
        } catch (...) {}

        c2.disconnect();
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
    });
}


// -----------------------------------------------------------------------------
// Test: Server crash while clients connected → next connection triggers restart.
// Verifies that onBackendConnectionFailed still correctly marks server OFF, so the fix
// to onBackendDisconnected doesn't break genuine failure detection.
// This test MUST PASS (confirms no regression in failure handling).
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_backend_connect_failure_still_marks_off) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        // Start EchoServer + ZzzProxy.
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5106); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy; // no cmds -> turnServerOn is a no-op
        thread proxyThread([&proxy]() { try { proxy.forward(6106, "localhost", 5106); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // Client connects and establishes backend channel.
        TcpClientB c1;
        try { c1.connect("localhost", 6106); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(400));
        assert(proxy.serverWasOn.load() == true && "Server should be ON after connect");

        // Kill backend (crash simulation).
        TcpClientB crasher;
        try {
            crasher.connect("localhost", 5106);
            crasher.send("shutdown");
        } catch (...) {}
        if (backendThread.joinable()) backendThread.join();
        this_thread::sleep_for(chrono::milliseconds(400));

        // New client connects → proxy tries to connect dead backend → fails.
        // onBackendConnectionFailed should set flag → isServerOn() returns false.
        TcpClientB c2;
        try { c2.connect("localhost", 6106); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(800));

        bool nowOff = (proxy.serverWasOn.load() == false);
        assert(nowOff && "BUG: Server still ON after backend connect failure — stale state not cleared");

        // Cleanup
        c1.disconnect();
        c2.disconnect();
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
    });
}


// -----------------------------------------------------------------------------
// Test: Rapid connect/disconnect cycle must NOT accumulate server restarts.
// Before fix: each disconnect set backendFailureDetected → next connect called turnServerOn() again.
// After fix:  backend close just logs; no restart triggered by normal lifecycle.
// This test MUST FAIL before fix and PASS after.
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_rapid_connect_disconnect_no_duplicate_starts) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5107); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy; // no cmds -> turnServerOn is a no-op
        thread proxyThread([&proxy]() { try { proxy.forward(6107, "localhost", 5107); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // Rapid connect/disconnect cycle: server must stay ON throughout.
        for (int i = 0; i < 3; i++) {
            TcpClientB c;
            try {
                c.connect("localhost", 6107);
                this_thread::sleep_for(chrono::milliseconds(200));

                // FIX CHECK: server should remain ON after each connect+disconnect.
                bool stayedOn = proxy.serverWasOn.load();
                assert(stayedOn);

                c.disconnect();
                this_thread::sleep_for(chrono::milliseconds(300));

                bool stillOnAfterDisconnect = proxy.serverWasOn.load();
                assert(stillOnAfterDisconnect);
            } catch (...) {}
        }

        // Final client verifies proxy is healthy.
        TcpClientB finalC;
        bool gotResp = false;
        try {
            finalC.connect("localhost", 6107);
            this_thread::sleep_for(chrono::milliseconds(300));
            finalC.send("final");
            if (TestTcpUtils::wait_for_available(finalC, 3000)) {
                string resp;
                try { resp = finalC.read(); } catch (...) {}
                gotResp = !resp.empty() && resp.find("final") != string::npos;
            }
        } catch (...) {}

        assert(gotResp && "Final connection should succeed after rapid cycles");

        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
    });
}


#endif // TEST

