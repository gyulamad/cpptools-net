# Zzz Proxy Documentation

## Overview

Zzz Proxy ("Sleep/Wake Proxy") is an intelligent TCP proxy server that automatically manages backend server lifecycle based on client activity. It extends [WolProxy](wol-proxy/wol-proxy.md) with **idle timeout shutdown support**, allowing servers to be powered off when unused and automatically started when clients connect.

This solves energy efficiency problems in home labs or small deployments where expensive GPU hardware should only consume power when actively processing requests—typical scenarios include AI/LLM serving endpoints like llama.cpp servers, build agents, or any compute-intensive background services.

```
Client ──► ZzzProxy ◄──(no activity for N minutes)──► Backend Server
              │                                              │
              └──── Idle Timeout Triggered ──────────────────┘
                    └──→ Execute Sleep Script ────────────────┤
                    
When new request arrives:
    Client ──► ZzzProxy ──→ Wake Script Executes ──► Backend Wakes Up ──► Forward Traffic
```

## Architecture & Components

### Class Hierarchy

| File | Description |
|------|-------------|
| [`ZzzProxy.hpp`](ZzzProxy.hpp:1) | Core implementation extending `TcpProxy` — contains idle timeout logic, activity tracking (`lastActivityTime`), server start/stop via commands |
| [`IpWhitelist.hpp`](IpWhitelist.hpp:1) | Middle-layer IP filtering abstraction — loads regex patterns from INI config file; blocks denied sources with alert log before proxy processes connection |
| [`ZzzProxyApplication.hpp`](ZzzProxyApplication.hpp:1) | CLI argument parsing wrapper around base App class; maps positional args into forward() call parameters |
| [`zzz-proxy.cpp`](zzz-proxy.cpp:1) | Minimal main entry point instantiating application |

### State Machine Flow Diagram

```
STARTUP → SERVER_UNKNOWN
    
    ├── First client connects while SERVER_UNKNOWN 
    │   └── Transition immediately to SERVER_UP (assume ready)
    │
    ├── No clients + idleTimeoutSec elapses + sleepCmd configured  
    │   └── triggerSleep() → execute "stop" command → SERVER_SLEEPING → SERVER_DOWN
    │
CLIENT DISCONNECTED IDLE TIMER RUNNING ←←←←←←←←←←←←←←┐
                                                      │
         ┌───────────────────────────────────────────┘
         ▼                                           
  When hasClients && !wakeupInProgress              
     during SERVER_DOWN                             
        └── triggerWakeup()                         
            → exec(wakecmd)                        
            → transition to SERVER_UP               
                                                      
While already in SERVER_UP after wake completes     
                                                   
If all clients disconnect again...                 
repeat cycle from top.
```

**Key behaviors:**
- The system tracks lastActivityTime timestamp updated whenever ANY client is connected (`hasClients = true`)
- Only triggers SLEEP transitions if BOTH conditions met: zero active connections AND elapsed time exceeds configured threshold AND a non-empty sleep script was provided at startup. This prevents accidental shutdowns of always-on infrastructure unless explicitly requested via configuration.
- Similarly WAKE only fires when fresh connection attempts arrive while server marked DOWN and no other wake sequence currently running—prevents duplicate concurrent executions causing race conditions or resource contention.

## Usage

```bash
./zzz-proxy <port> <backend> --startcmd=<wake_cmd> --stopcmd=<sleep_cmd> [--timeout=N] [--whitelist=config.ini]
```

### Arguments Reference Table

#### Positional Arguments

| Position | Name    | Required | Default | Description                                                      |
|:--------:|:-------:|:--------:|---------|------------------------------------------------------------------|
| 1        | port    | Yes      | —       | Local TCP listening port for proxy                               |
| 2        | backend | Yes      | —       | Target service address as `<host>:<port>` format                 |

#### Named Options (passed via `--key=value`)

| Option     | Required | Default | Description                                                                                      |
|:-----------|:--------:|---------|--------------------------------------------------------------------------------------------------|
| startcmd   | Yes      | —       | Script path OR shell command executed when first client connects and server is off               |
| stopcmd    | Yes      | —       | Shell command triggered after idle timeout with NO active client connections                     |
| timeout    | No       | `300`   | Idle seconds before triggering the **stop** action; must be > 0                                  |
| whitelist  | No       | *(none)*| Path to INI config file with `[whitelist]` section containing regex patterns for IP filtering    |

#### Argument Details & Constraints

*Named Options:*  
The implementation uses named options (`--key=value`) rather than positional arguments for optional parameters. See [`ZzzProxyApplication.hpp`](ZzzProxyApplication.hpp:26) lines ~21–47 for exact mapping between option names and internal variable names:

```
arg[1]=port, arg[2]="host:port"
--startcmd=path/to/start-script-or-command-line
--stopcmd=path/to/stop-script-or-command-line
--timeout=idle-timeout-seconds (default: 300)
--whitelist=path/to/ip-whitelist.ini (optional)
```

### IP Whitelist Filtering

ZzzProxy supports filtering incoming connections by source IP address using a whitelist of regex patterns loaded from an INI configuration file. When enabled, any connection from an IP that does **not** match at least one pattern in the whitelist is immediately rejected with an alert log entry — the proxy performs no further processing (no backend connection, no idle timer update).

**Config File Format:**
```ini
[whitelist]
localnet = ^192\.168\.\d+\.\d+$
datacenter = ^10\.0\.0\.[0-9]+$
localhost = ^127\.0\.0\.1$
```

The section name must be `[whitelist]`. The keys are ignored; only the values (regex patterns) matter. Patterns use ECMAScript regex syntax and match against the IP portion of the client address (port is stripped before matching). If no whitelist file is specified, or if the file has no `[whitelist]` section, all connections are allowed.

**Usage Example:**
```bash
./zzz-proxy 8080 localhost:3000 \
    --startcmd=./srv/start.sh \
    --stopcmd=./srv/stop.sh \
    --timeout=600 \
    --whitelist=/etc/zzzproxy/allowed.ini
```

When a denied IP connects, the log shows:
```
[alert] Blocked connection from denied source: 10.99.88.77:54321
```

The whitelist feature is implemented as a separate [`IpWhitelist`](IpWhitelist.hpp:1) class that can be reused independently of ZzzProxy. See the embedded unit tests in that file for pattern matching behavior details.

### Examples

**Basic usage without power management scripts:**
```bash
# Just forward traffic locally - same as plain TcpProxy:
./zzz-proxy 8080 localhost:3000 
```

**With custom wake/sleep commands using default timeouts:**

Assuming you have executable scripts in current directory named `start-llm.sh` / `stop-llm.sh`:

```bash
./zzz-proxy \
    8080 \
    monster.local:100000 \
    ./srv/ai/start.sh \
    ./srv/ai/stop.sh
```

Note that argument positions matter—omitting optional WOL params (#3,#4) means they won't appear anywhere else either so script execution only triggers based purely upon presence absence of clients relative configured thresholds regardless hardware-level remote powering capabilities via NIC magic packets.

But if your infrastructure requires BOTH software start AND physical boot via WoL then include them too:

```bash
./zzz-proxy \
    8080 \
    ai-server.home.internal:8000 \     # backend host+port
    192.168.1.255 \                      # broadcast IP (subnet)
    AA:BB:CC:DD:EE:F0 \                  # MAC address of GPU node NIC  
    "./my-wake-sequence"                # combined or separate? see below...
```

Wait! The above example shows mixing both old-style Wake-on-LAN plus new "script-based startup". However note parameter ordering semantics—the code treats args [3]=[wolip], [4]=[mac]; these are independent from wakecmd at pos#5 which can contain ANY command string including calling another shell wrapper containing its own wakeonlan call internally—so choose whichever approach matches deployment model best; avoid trying to pass multiple responsibilities through single slot unless handled by outer orchestration layer outside this program itself.

**Custom idle timeout value shorter than defaults**

If wanting faster shutdown after last client disconnects e.g., test automation scenarios where resources must release quickly:

```bash
./zzz-proxy 9000 my-backend-host.example.com:443 "" "" "/opt/scripts/shutdown-service.sh" "/opt/scripts/restart-service.sh" 60
```
(Here we explicitly set all preceding positional placeholders even though empty strings—they're required because final param is position=7)

## How It Works — Internal Execution Flow

The proxy runs an event loop processing network I/O and performing periodic housekeeping tasks each iteration (~every ~100ms). Within [`ZzzProxy::onTick()`](ZzzProxy.hpp:123):

### Step-by-step sequence during normal operation:

1. **Initialize**: On first launch, sets initial state = SERVER_UNKNOWN with timestamp recorded for future reference.
2. **Accept connections**: When a client connects:
   - If server currently marked DOWN → triggerWakeup() executes the provided `--wakecmd`
   - Otherwise just forward traffic normally as any TCP relay would do
   
3. **Idle monitoring** (runs once per second):
   ```
   tickCounter increments every onTick()
   when reaches TICKS_PER_CHECK (=10), perform full evaluation:
       hasClients ? update(lastActivityTime=NOW) : check elapsed duration since lastActivityTime 
       
       switch(serverState):
           case UP:
               // Only consider sleeping when ALL clients gone + threshold exceeded + script configured!
               if (!hasClients && idleDuration >= idleTimeoutSec && !sleepCmd.empty())
                   triggerSleep(); // exec(sleepcmd); transition -> SLEEPING then -> DOWN
               
               else if no activity but not yet reached limit:
                   log "[UP] Idle Xs / timeout Ys"
           
           case DOWN:
               if(hasClients && !wakeupInProgress)
                   triggerWakeup();
                   
           default: break;
   ```

4. **Script execution details:**
   
   Both `triggerWakeup()` ([line~74-112)](ZzzProxy.hpp:74)) and `triggerSleep()` ([lines206-230)](ZzzProxy.hpp:206)):
   - Use same underlying helper function `exec(cmd,true)` that spawns child process via fork+execlp pattern—blocks until completion or throws exception on failure  
   - Catch exceptions gracefully so one failing command doesn't crash entire service; logs error messages using LOG_ERROR macro before reverting to previous safe fallback states accordingly  

5. **Graceful degradation:** Even without scripts defined (`empty string`), behavior reduces back to simple pass-through forwarding—no automatic power management occurs unless both wake/sleep commands are supplied.

## Helper Scripts Provided Under `./srv/`

This distribution includes ready-to-use example automation wrappers in subdirectory structure:

| Path | Purpose |
|------|---------|
| [`goto.sh`](srv/goto.sh) | Reusable bash label-jump utility enabling retry loops within other shell scripts |
| [`ai/wakeup.sh`](srv/ai/wakeup.sh) | Sends Wake-on-LAN magic packet targeting MAC address 50:65:F3:2D:45:3F via broadcast IP 192.168.1.255 |
| [`ai/stop.sh`](srv/ai/stop.sh) | SSH into remote host "monster" as user gyula, runs pkill for llama.cpp processes with auto-retry loop logic |
| [`ai/start.sh`](srv/ai/start.sh) | Calls wakeup first THEN launches LLM server startup sequence over SSH passing model name parameter |

These serve as templates—you'll likely customize them based upon your own infrastructure specifics (hostnames/IP addresses/MAC addresses/service names).

### Integration Example

Assuming you have a GPU workstation named 'monster' running an AI inference endpoint listening locally at port 100000 accessible only after boot-up completes through network share etc., the full deployment could look like this from client perspective:

```
┌─────────────────────────────────────────────────────────────┐
│ Your Laptop                                               │
└── http://localhost:8080/api/generate ──► Zzz Proxy        │
                                          │                  │
                              ┌───────────┴───────────────┐ 
                              ▼                           ▼
                        [if monster offline → run start script]
                              └───────────┬───────────────┘
                                          ▼
                                   TCP tunnel established
                                    to target backend:
                                 monster.local:100000
```

The proxy automatically handles all intermediate steps transparently behind scenes—the calling application just sees normal HTTP responses once connection succeeds!

## Requirements & Dependencies

**System utilities required:**
- Standard POSIX environment (Linux/macOS)
- C++17 compiler toolchain — project uses modern language features throughout cpptools libraries.
- `bash` interpreter if utilizing provided helper wrapper scripts under srv/
- Optional but recommended when leveraging hardware-level remote powering capabilities:
    * `wakeonlan` package for sending magic packets  
    * Network interface configured to forward broadcasts appropriately across VLAN boundaries if needed
    
**Build requirements**: See parent directory's build system; typically handled by top-level CMakeLists.txt or autobuild configuration files present elsewhere in repository tree—refer there for exact compilation instructions specific to platform and desired optimization levels.

## Limitations & Notes

* The idle timer resets whenever ANY active client exists—even single persistent WebSocket keeping open prevents sleep indefinitely regardless of request frequency! Plan accordingly e.g., implement heartbeat timeouts on clients side that close connections during periods inactivity so downstream can properly detect true absence state.
* Scripts execute synchronously within event-loop tick context—they block further processing until completion which may cause brief delays before new requests get forwarded especially if external commands take many seconds/minutes themselves performing heavy initialization tasks such loading large ML models memory onto GPUs . Consider using background daemonization inside those wrappers via nohup/setsid patterns similar how base WOL implementation does it.  
* If both wolip+mac AND wakecmd are specified, they operate independently—the former sends raw Ethernet frames while latter runs arbitrary shell code—so choose whichever matches actual power management approach used physical vs software level control signals respectively rather than mixing unless aware interactions between layers might produce unexpected results due timing issues around NIC wake triggers versus service startup ordering expectations relative each other .