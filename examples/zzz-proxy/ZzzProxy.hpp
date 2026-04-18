// =============================================================================
// ZzzProxy.hpp — Sleep/Wake Proxy with Idle Timeout Shutdown Support
// =============================================================================

#pragma once

#include "../wol-proxy/WolProxy.hpp"
#include "../../../misc/default_get_time_sec.hpp"

using namespace std;

// Server state enumeration  
enum ServerState { SERVER_UNKNOWN=0, SERVER_UP, SERVER_DOWN, SERVER_WAKING, SERVER_SLEEPING };

class ZzzProxy : public WolProxy {
public:
    // Constructor initializes all member variables 
    ZzzProxy() : WolProxy(), lastClientCount(0), serverState(SERVER_UNKNOWN),
                 idleTimeoutSec(300), tickCounter(0), wakeupInProgress(false) {}
    
    virtual ~ZzzProxy() {}

    void forward(int port, const string& host, int hport,
                 const string& wolip = "", const string& mac = "",
                 const string& wakeCmd_ = "",   // script/path to start/wake service
                 const string& sleepCmd_ = "",  // script/path to stop/idle service  
                 int idleSeconds = 300) {       // default 5 min idle
        
        this->wakeCmd = wakeCmd_;
        this->sleepCmd = sleepCmd_;
        this->idleTimeoutSec = (idleSeconds > 0) ? idleSeconds : 60;  // minimum 60 sec
        
        LOG("ZzzProxy config:");
        LOG("  Backend: " + host + ":" + to_string(hport));
        
        if (!wolip.empty()) {
            LOG("  WOL IP: " + wolip);
            LOG("  MAC: " + mac);
        }
        
        if (!wakeCmd.empty())
            LOG(string("Wake cmd: ") + wakeCmd);
            
        if (!sleepCmd.empty())
            LOG(string("Sleep cmd: ") + sleepCmd);
            
        LOG(string("Idle timeout: ") + to_string(this->idleTimeoutSec) + " seconds");
        
        TcpProxy::forward(port, host, hport);
        
        // Initialize activity time on startup
        lastActivityTime = getCurrentTimeSec();
    }

protected:
    /// Member variables required by the implementation
    int lastClientCount;
    ServerState serverState;
    int idleTimeoutSec;
    int tickCounter;
    string wakeCmd;
    string sleepCmd;
    time_t lastActivityTime;
    bool wakeupInProgress;

private:
    inline static time_t getCurrentTimeSec() {
        return default_get_time_sec();
    }

public:

/// Execute configured wake command via exec()
void triggerWakeup(){
if(wakeupInProgress||wakeCmd.empty()){
return;}
wakeupInProgress=true;
// Save previous state before transitioning
ServerState prevState=serverState;
serverState=SERVER_WAKING;

// Log transition for debugging purposes

LOG("[WAKE] Executing wake-up command...");

try{
exec(wakeCmd,true);

// Transition UP after successful execution  

serverState=SERVER_UP;

// Update timestamp tracking recent activity 
lastActivityTime=getCurrentTimeSec();

LOG("[WAKE] Command completed. Server now UP.");

// Add delay so we don't repeatedly issue commands - skip next few check cycles 

tickCounter+=30;// ~3 secs at current rate (~100ms per cycle)

}catch(const exception& e){
// On failure revert and log error message accordingly.
LOG_ERROR(string("[WAKE] Failed executing command! Exception caught: ") + e.what());
serverState=(prevState==SERVER_UNKNOWN?SERVER_DOWN:prevState);// Revert or stay DOWN depending context.

}

wakeupInProgress=false;


}

/// Triggered when backend disconnects unexpectedly — update internal availability status.
/// Note that base class doesn't expose explicit callback hook here but can infer from client-side events indirectly through other means later as needed.
// void onBackendDisconnect(int fd){...}
    
/**
 * Called each event loop iteration (every ~100 ms).
 *
 * Monitors connection count vs thresholds; triggers appropriate actions based on elapsed idleness durations beyond configured limits.
 */
   void onTick() override {       
       constexpr int TICKS_PER_CHECK = 10; // Check once/sec instead of waiting full cycle
      
      ++tickCounter; 

     if(tickCounter >= TICKS_PER_CHECK){  

         tickCounter = 0;

          const bool hasClients = !clients.empty();
           const time_t now = getCurrentTimeSec();

// Track any new incoming data/activity to reset idle timer appropriately  
            if(hasClients)
                lastActivityTime = now;


switch(serverState){

case SERVER_UNKNOWN:{   
     
     // If no clients yet, assume down initially until proven otherwise upon first request arrives...
      
break;}    

case SERVER_DOWN:{
// When a fresh client appears while we're marked offline AND not already waking up,
if(hasClients && !wakeupInProgress){
triggerWakeup(); // Bring server back online!
}
 break;}    

 case SERVER_UP:{

int idleDurationSeconds = static_cast<int>(now - lastActivityTime);
const string idleInfo="Idle "+to_string(idleDurationSeconds)+"s / timeout "+to_string(idleTimeoutSec)+"s";

// Only consider sleeping when ALL clients have disconnected completely:
if(!hasClients && idleDurationSeconds >= idleTimeoutSec && !sleepCmd.empty()){ 


 triggerSleep();


} else if (!hasClients) {
   // Convert enum value into human-readable label manually via switch-case or simple mapping approach:
    const char* stateLabel = "?";
       switch(serverState){
           case SERVER_UP:      stateLabel="UP"; break;
           case SERVER_DOWN:   stateLabel="DOWN"; break;
           case SERVER_WAKING: stateLabel="WAKING"; break;
           case SERVER_SLEEPING:stateLabel="SLEEPING";break;
           default:             stateLabel="UNKNOWN";break;}
    
    LOG(string("[") + stateLabel + "] " + idleInfo);
}

 break;
}

// While transitioning between states just wait for operations in progress...

default:

  break;
 
 }


 lastClientCount=static_cast<int>(clients.size());


 }

// Continue processing parent class logic normally
   
    WolProxy::onTick();
 }

/**
* Execute the sleep script via exec()
*/
private:
void triggerSleep(){
if(sleepCmd.empty()){
return;}
        
        LOG("[SLEEP] Idle threshold reached. Executing shutdown...");

try{
exec(sleepCmd,true);

// After successful execution transition state accordingly

serverState=SERVER_SLEEPING;

// Log confirmation message indicating operation completed successfully!

LOG("[SLEEP] Command sent. Server going down.");

}catch(const exception& e){
// On failure log error and revert to DOWN state.

LOG_ERROR(string("[SLEEP] Failed executing command! Exception caught: ") + e.what());
serverState=SERVER_DOWN;
}

}


};