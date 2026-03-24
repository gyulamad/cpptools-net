# WOL Proxy Documentation

## Overview

The WOL (Wake-on-LAN) Proxy is a TCP proxy server that automatically wakes up a remote server via Wake-on-LAN before forwarding client connections to it. This allows servers to be powered off when not in use and automatically started when needed.

## Architecture

```
Client → WOL Proxy (local) → [WOL Magic Packet] → Target Server (wakes up) → Client
```

## Components

### [`wol.hpp`](wol.hpp:1)
Contains the `wol()` function that:
1. Pings the target server to check if it's awake
2. Sends Wake-on-LAN magic packets if needed
3. Retries up to 20 times with 5-second intervals

### [`WolProxy.hpp`](WolProxy.hpp:1)
Extends [`TcpProxy`](TcpProxy.hpp:1) with WOL functionality:
- Sends WOL packet on first client connection
- Executes optional startup command via SSH after WOL succeeds

### [`wol-proxy.cpp`](wol-proxy.cpp:1)
Main application entry point that parses command-line arguments and starts the proxy.

## Usage

```bash
./wol-proxy <port> <address> <wolip> <mac> [user] [cmd]
```

### Arguments

| Position | Name | Required | Description |
|----------|------|----------|-------------|
| 1 | `port` | Yes | Local port number for the proxy to listen on |
| 2 | `address` | Yes | Target server address in format `<host>:<port>` |
| 3 | `wolip` | Yes | WOL broadcast IP address (e.g., `255.255.255.255`) |
| 4 | `mac` | Yes | Target server's MAC address (e.g., `AA:BB:CC:DD:EE:FF`) |
| 5 | `user` | Optional | SSH username for running startup command |
| 6 | `cmd` | Optional | Shell command to execute on the target server after wake-up |

### Examples

**Basic usage (no startup command):**
```bash
./wol-proxy 8080 192.168.1.100:3000 255.255.255.255 AA:BB:CC:DD:EE:FF
```

**With SSH startup command:**
```bash
./wol-proxy 8080 192.168.1.100:3000 255.255.255.255 AA:BB:CC:DD:EE:FF user "cd /home/user/myapp && ./server"
```

## How It Works

1. **Proxy starts** - Listens on the specified local port
2. **First client connects** - Proxy detects the connection attempt
3. **WOL sequence**:
   - Pings target server (1 second timeout)
   - If unreachable, sends Wake-on-LAN magic packet
   - Retries ping up to 20 times (5 seconds between attempts)
4. **Startup command** (optional):
   - After successful WOL, executes `ssh user@host 'setsid cmd >/dev/null 2>&1 < /dev/null &'`
   - The `setsid` detaches the command from SSH session
   - Command runs only once (on first connection)
5. **Proxy forwards** - TCP traffic between client and target server

## WOL Function Details

```cpp
bool wol(
    const string& wolip,    // Broadcast IP (e.g., "255.255.255.255")
    const string& mac,      // Target MAC address
    const string& addr,     // Target server address
    const string& cmd_ping = "ping -c 1 -W 1 {{addr}}",
    const string& cmd_wol = "wakeonlan -i {{wolip}} {{mac}}",
    int retry = 20,
    function<void(int)> callback = [](int retry) { cout << "[WOL] retry " << retry << "..." << endl; }
)
```

Returns `true` if server was woken up, `false` if already awake or timeout occurred.

## Requirements

- `wakeonlan` utility for sending WOL packets
- `ping` utility for checking server status
- SSH access (if using startup command)
- Network support for WOL packets (broadcast or unicast)

## Notes

- The startup command runs only once, on the first client connection
- Subsequent connections go directly to the already-woken server
- If the server is already awake, WOL is skipped
- The proxy continues running even if WOL fails (returns `false`)