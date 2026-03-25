#pragma once

#ifdef TEST

#include <thread>
#include <chrono>
#include "../TcpProxy.hpp"
#include "../EchoServer.hpp"
#include "../TcpClientB.hpp"

class TestTcpUtils {
public:

    // Helper: wait for a client to have data available (with timeout)
    static bool wait_for_available(TcpClientB& client, int timeoutMs = 2000) {
        auto start = chrono::steady_clock::now();
        while (!client.available()) {
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs) return false;
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        return true;
    }

    // Helper: wait for port to be available after test
    static void wait_for_port_release(int delayMs = 1000) {
        this_thread::sleep_for(chrono::milliseconds(delayMs));
    }

    // Helper: shutdown backend and stop proxy
    static void shutdown_test_resources(EchoServer& /*backend*/, thread& backendThread, 
                                        TcpProxy& proxy, thread& proxyThread,
                                        int backendPort) {
        // Send shutdown to backend directly
        TcpClientB shutdownClient;
        // try {
            shutdownClient.connect("localhost", backendPort);
            shutdownClient.send("shutdown");
        // } catch (...) {
        //     // Ignore if shutdown fails
        // }
        
        // Wait for backend to stop
        if (backendThread.joinable()) backendThread.join();
        
        // Stop proxy
        proxy.stop();
        if (proxyThread.joinable()) proxyThread.join();
    }

};

#endif