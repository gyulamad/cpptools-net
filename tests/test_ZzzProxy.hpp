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
// ZzzProxy Tests — covers sleep/wake proxy state tracking bugs
// Port naming: backend=51XX, proxy=61XX
// =============================================================================

// Helper subclass to expose protected members for testing.
struct TestableZzzProxy : public ZzzProxy {
    bool checkIsServerOn() const { return isServerOn(); }
};

// -----------------------------------------------------------------------------
// Test: After unexpected backend crash + restart, a new client connecting through
//        the proxy should be able to communicate with the (newly started) backend.
// BUG: With current code, stale lastActivityTime means isServerOn() returns true,
//      so turnServerOn() is never called and no reconnection happens → failure.
// This test MUST FAIL before fix and PASS after.
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_reconnects_after_backend_crash) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        // Phase 1: Start backend + ZzzProxy (no start/stop cmds, just forwarding)
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5103); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy; // no startCmd/stopCmd — we manually control the "server" (EchoServer)
        thread proxyThread([&proxy]() { try { proxy.forward(6103, "localhost", 5103); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // First client connects → sets activity time > 0. Proxy thinks server is ON.
        TcpClientB c1;
        try { c1.connect("localhost", 6103); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(500));

        assert(proxy.checkIsServerOn() == true && "After connect proxy should think server is ON");

        // Phase 2: Kill the backend unexpectedly (simulating crash).
        TcpClientB crasher;
        try {
            crasher.connect("localhost", 5103);
            crasher.send("shutdown");
        } catch (...) {}
        if (backendThread.joinable()) backendThread.join();
        this_thread::sleep_for(chrono::milliseconds(400));

        // BUG: After backend dies, isServerOn() still returns true because nothing resets lastActivityTime.
        bool staleState = proxy.checkIsServerOn();
        assert(staleState == false && "BUG: Proxy has stale state — thinks server ON after crash");

        // Cleanup
        c1.disconnect();
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
    });
}


// -----------------------------------------------------------------------------
// Test: When backend connection fails for a client, the proxy should detect this
//        and reset its server state so that subsequent connections can trigger recovery.
// BUG: Currently processBackends in TcpProxy.hpp prints "Backend connection failed" but does NOT
//      notify ZzzProxy to clear stale state → all future clients also fail.
// This test MUST FAIL before fix, PASS after.
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_clears_stale_state_on_backend_failure) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        // Phase 1: Start backend + ZzzProxy briefly to establish "server is ON" state.
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5104); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy;
        thread proxyThread([&proxy]() { try { proxy.forward(6104, "localhost", 5104); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // Client connects → activity time set.
        TcpClientB c1;
        try { c1.connect("localhost", 6104); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(500));

        assert(proxy.checkIsServerOn() == true && "Proxy should think server is ON");

        // Phase 2: Kill backend abruptly.
        TcpClientB crasher;
        try {
            crasher.connect("localhost", 5104);
            crasher.send("shutdown");
        } catch (...) {}
        if (backendThread.joinable()) backendThread.join();
        this_thread::sleep_for(chrono::milliseconds(300));

        // Phase 3: New client connects — proxy tries to connect backend, fails.
        TcpClientB c2;
        try {
            c2.connect("localhost", 6104);
            this_thread::sleep_for(chrono::milliseconds(800));
        } catch (...) {}

        // After the backend failure is detected, proxy should have cleared stale state.
        bool stillStale = proxy.checkIsServerOn();
        assert(stillStale == false && "BUG: Proxy did NOT clear stale state after detecting backend failure");

        // Cleanup
        c1.disconnect();
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
    });
}


// -----------------------------------------------------------------------------
// Test: Full recovery cycle — backend crashes, new client connects through proxy.
//        Proxy detects stale state, clears it, and the next connection attempt
//        should succeed when a real server is listening again.
// This test MUST FAIL before fix, PASS after.
// -----------------------------------------------------------------------------
TEST(test_ZzzProxy_full_recovery_after_backend_crash) {
    TestTcpUtils::wait_for_port_release();

    capture_cout_cerr([]() {
        // Phase 1: Start backend + ZzzProxy (no startCmd/stopCmd).
        EchoServer backend;
        thread backendThread([&backend]() { try { backend.listen(5105); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        TestableZzzProxy proxy;
        thread proxyThread([&proxy]() { try { proxy.forward(6105, "localhost", 5105); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // First client connects → activity time > 0.
        TcpClientB c1;
        try { c1.connect("localhost", 6105); } catch (...) {}
        this_thread::sleep_for(chrono::milliseconds(400));

        assert(proxy.checkIsServerOn() == true && "Proxy thinks server is ON");

        // Phase 2: Kill the backend.
        TcpClientB crasher;
        try {
            crasher.connect("localhost", 5105);
            crasher.send("shutdown");
        } catch (...) {}
        if (backendThread.joinable()) backendThread.join();
        this_thread::sleep_for(chrono::milliseconds(300));

        // Phase 3: Start a new EchoServer on same port (simulating server restart).
        EchoServer backend2;
        thread backendThread2([&backend2]() { try { backend2.listen(5105); } catch (...) {} });
        this_thread::sleep_for(chrono::milliseconds(200));

        // Phase 4: New client connects through proxy. Sends a message, expects echo response.
        TcpClientB c2;
        bool success = false;
        try {
            c2.connect("localhost", 6105);
            this_thread::sleep_for(chrono::milliseconds(300));
            c2.send("recovery_test");

            // EchoServer drips one char/second, so "recovery_test" (13 chars + newline = 14) takes ~1.4s.
            bool gotResponse = TestTcpUtils::wait_for_available(c2, 5000);
            string resp;
            if (gotResponse) { try { resp = c2.read(); } catch (...) {} }

            // If we got ANY response from backend through proxy, the fix works.
            success = !resp.empty() && resp.find("recovery") != string::npos;
        } catch (...) {}

        bool stateCleared = (proxy.checkIsServerOn() == false); // After crash detection, should be OFF or recovering

        // Cleanup
        c1.disconnect();
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
        this_thread::sleep_for(chrono::milliseconds(300));

        TcpClientB shutdown2;
        try {
            shutdown2.connect("localhost", 5105);
            shutdown2.send("shutdown");
        } catch (...) {}
        if (backendThread2.joinable()) backendThread2.join();

        assert(success && "BUG: Proxy did NOT recover after backend crash+restart — stale state prevented reconnection");
    });
}


#endif // TEST

