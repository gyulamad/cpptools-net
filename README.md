# tcpfwd - TCP Forwarder with Wake-on-LAN

A TCP proxy that forwards traffic to a remote server with Wake-on-LAN (WOL) capability and automatic shutdown on idle.

## Architecture

```
Client → WolProxy → TcpProxy → TcpClientNB → Remote Server
                                    ↓
                            Wake-on-LAN Broadcast
```

## Components

| File | Purpose |
|------|---------|
| [`TcpServer.hpp`](TcpServer.hpp:1) | Base class for TCP servers with event loop, client management |
| [`TcpClientNB.hpp`](TcpClientNB.hpp:1) | Non-blocking TCP client for proxy connections |
| [`TcpProxy.hpp`](TcpProxy.hpp:1) | Forwards client connections to backend server |
| [`WolProxy.hpp`](WolProxy.hpp:1) | Extends TcpProxy with WOL and SSH startup |
| [`wol.hpp`](wol.hpp:1) | Wake-on-LAN utility using `wakeonlan` command |
| [`ping.hpp`](ping.hpp:1) | Ping utility for server availability check |
| [`wol-proxy.cpp`](wol-proxy.cpp:1) | Main entry point with CLI argument parsing |
| [`auto-shutdown.sh`](auto-shutdown.sh:1) | Server-side script to shutdown after N seconds idle |

## How It Works

1. **Proxy starts** listening on a port
2. **Client connects** → Proxy pings backend server
3. **If server is down** → Sends WOL magic packet, waits for server to wake
4. **Once server is up** → SSH command starts the backend service (e.g., `llamacpp-server.sh`)
5. **Traffic forwarded** bidirectionally between client and server
6. **Server-side** `auto-shutdown.sh` monitors log files and shuts down after idle timeout

## Usage

### Build

```bash
./builder wol-proxy.cpp --mode=strict
```

### Run

```bash
.build/strict/wol-proxy <port> <server>:<port> <wol-ip> <mac> <ssh-user> <ssh-cmd>
```

### Example

```bash
.build/strict/wol-proxy 9091 192.168.1.197:8080 192.168.1.255 50:65:F3:2D:45:3F gyula "./llamacpp-server.sh Qwen3 200000"
```

## Server Setup

1. Copy [`auto-shutdown.sh`](auto-shutdown.sh:1) to the server
2. Call it in your startup script (e.g., `llamacpp-server.sh`)

### auto-shutdown.sh Usage

```bash
./auto-shutdown.sh <check_interval_seconds> <max_age_seconds> <log_file>...
```

Example:
```bash
./auto-shutdown.sh 5 600 llamacpp.log
```

This will shutdown the server when:
- No SSH users are logged in (`who | grep pts/`)
- All specified log files are older than `max_age_seconds`

## Features

- **Non-blocking I/O** using `select()` event loop
- **Pending data buffering** during backend connection establishment
- **Graceful shutdown** - flushes send queue before closing connections
- **WOL integration** via shell commands
- **SSH startup** for remote server services

## Requirements

- `wakeonlan` command-line tool
- SSH access to the remote server
- SSH key-based authentication (no password prompts)