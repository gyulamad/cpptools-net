#pragma once

// =============================================================================
// IpWhitelist.hpp — IP-based connection whitelist filter
//
//   Loads regex patterns from an INI config file and checks incoming
//   client addresses against them. If no pattern matches, the connection
//   is considered denied.
//
//   Config file format (INI):
//     [whitelist]
//     ip1 = ^192\.168\.\d+\.\d+$
//     ip2 = ^10\.0\.0\.[0-9]+$
//     localhost = ^127\.0\.0\.1$
//
//   The keys are ignored; only the values (regex patterns) matter.
//   An empty whitelist file or missing [whitelist] section means ALL
//   connections are allowed (no filtering).
// =============================================================================

#include "../misc/IniFile.hpp"
#include "../misc/regx_match.hpp"
#include "../misc/Logger.hpp"
#include <vector>
#include <string>

using namespace std;


class IpWhitelist {
public:
    IpWhitelist() {}
    virtual ~IpWhitelist() {}

    // Load whitelist patterns from an INI config file.
    // If the file is empty or has no [whitelist] section, filtering is disabled.
    void load(const string& configFile) {
        this->configFile = configFile;
        patterns.clear();
        filteringEnabled = false;

        if (configFile.empty())
            return;

        IniFile ini(configFile, true, false, true, false);
        const DataT& data = ini.getDataCRef();

        auto it = data.find("whitelist");
        if (it == data.end()) {
            LOG("[IpWhitelist] No [whitelist] section found in " + configFile + " — filtering disabled");
            return;
        }

        for (const auto& kv : it->second) {
            if (!kv.second.empty()) {
                patterns.push_back(kv.second);
            }
        }

        filteringEnabled = !patterns.empty();
        LOG("[IpWhitelist] Loaded " + to_string(patterns.size()) + " pattern(s) from " + configFile);
    }

    // Check if the given address (format: "IP:port") is allowed.
    // Returns true if allowed, false if denied.
    // When filtering is disabled (no config or no patterns), always returns true.
    bool isAllowed(const string& addr) const {
        if (!filteringEnabled)
            return true;

        // Extract IP from "IP:port" format
        string ip = extractIp(addr);

        for (const auto& pattern : patterns) {
            if (regx_match(pattern, ip)) {
                return true;
            }
        }

        return false;
    }

    // Check and log denial if IP is not allowed.
    // Returns true if connection should proceed, false if it should be dropped.
    bool check(const string& addr) const {
        if (isAllowed(addr))
            return true;

        LOG_ALERT("Blocked connection from denied source: " + addr);
        return false;
    }

    // Extract IP address from "IP:port" format
    static string extractIp(const string& addr) {
        size_t colonPos = addr.rfind(':');
        if (colonPos != string::npos)
            return addr.substr(0, colonPos);
        return addr;
    }

    bool isEnabled() const { return filteringEnabled; }
    const vector<string>& getPatterns() const { return patterns; }

#ifdef TEST
    // Test-only: set patterns directly without loading from file
    void setPatterns(const vector<string>& p) {
        patterns = p;
        filteringEnabled = !patterns.empty();
    }
#endif

protected:
    string configFile;
    vector<string> patterns;
    bool filteringEnabled = false;
};

