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


#endif // TEST

