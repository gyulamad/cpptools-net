#pragma once

#include <string>
#include <functional>
#include <iostream>
#include "../misc/Curl.hpp"
#include "../misc/sleep.hpp"
#include "../misc/JSON.hpp"

using namespace std;

// Check if llama.cpp server is ready (model loaded)
// Returns true when /health returns 200 with {"status": "ok"}
inline bool wol_ready(
    const string& host,
    int port,
    int retry = 30,
    int delay_ms = 2000,
    function<void(int)> callback = [](int retry) {
        cout << "[READY] retry " << retry << "..." << endl;
    }
) {
    Curl curl;
    string url = "http://" + host + ":" + to_string(port) + "/health";
    
    for (int i = 0; i < retry; i++) {
        if (i > 0) {
            callback(retry - i);
            sleep_ms(delay_ms);
        }
        
        string response;
        bool ok = curl.GET(url, [&](const string& chunk) {
            response += chunk;
        });
        
        if (!ok) continue;
        
        // Parse JSON response
        JSON json(response);
        
        // Check for success status
        if (json.has("status") && json.isString("status") && json.get<string>("status") == "ok") {
            return true;
        }
        
        // Check for loading error (503)
        if (json.has("error") && json.isObject("error")) {
            JSON jerror = json.get<JSON>("error");
            if (jerror.has("code") && jerror.isInt("code") && jerror.get<int>("code") == 503) {
                continue; // Model still loading, retry
            }
        }
    }
    
    return false;
}