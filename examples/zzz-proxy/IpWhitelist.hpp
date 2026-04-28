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

#include "../../../misc/IniFile.hpp"
#include "../../../misc/regx_match.hpp"
#include "../../../misc/Logger.hpp"
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

        LOG("[ALERT] Blocked connection from denied source: " + addr);
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


#ifdef TEST
#include "../../../misc/TEST.hpp"
#include "../../../misc/NullLogger.hpp"

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

#endif
