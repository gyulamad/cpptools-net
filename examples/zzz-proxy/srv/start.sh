#!/bin/bash

set -euo pipefail

MAC_ADDRESS="50:65:F3:2D:45:3F"
BROADCAST_ADDR="192.168.1.255"

TARGET_IP="192.168.1.197"       # IP address of machine being started  
SSH_USER="gyula"
SERVER_SCRIPT="./llamacpp-server.sh MiniMax-M2.5 80000 >> ./llamacpp-server.log 2>&1 &"

HEALTH_HOST="monster"           # Hostname after DNS resolution /etc/hosts entry  
HEALTH_PORT=8080                # Port where llama.cpp server listens    
HEALTH_ENDPOINT="/v1/health"

MAX_PING_RETRIES=180             # Max pings while waiting for boot (~60s)     
PING_INTERVAL=1                 

MAX_HEALTH_RETRIES=180          # Max attempts checking llm server readiness (~600s max)
HEALTH_RETRY_DELAY=5            # Seconds between each retry  

echo "[START] Sending WoL magic packet..."
wakeonlan -i "$BROADCAST_ADDR" "$MAC_ADDRESS"

echo "[START] Waiting for $TARGET_IP to come online..."

for attempt in $(seq 1 $MAX_PING_RETRIES); do
    
    if ping -c 1 -W 1 "$TARGET_IP" &>/dev/null; then
        
        echo "[START] Host responded after ${attempt} ping(s)."
        
        break
        
    fi
    
done

if ! ping -c 1 -W 1 "$TARGET_IP" &>/dev/null; then
    
    echo "[ERROR] Target never came online."
    exit 1
    
fi

# Give SSH service time to initialize on newly-booted system
echo "[START] Waiting +5s for SSH daemon..." 
sleep 5

echo "[START] Starting LlamaCpp server over SSH..."
echo "Execute: ${SSH_USER}@${TARGET_IP}" "$SERVER_SCRIPT"
ssh -o ConnectTimeout=10 \
    -o StrictHostKeyChecking=no \
    -x "${SSH_USER}@${TARGET_IP}" "$SERVER_SCRIPT"

# Now wait until the LLM API becomes available and healthy
echo "[START] Polling health endpoint at http://${HEALTH_HOST}:${HEALTH_PORT}${HEALTH_ENDPOINT}"

for attempt in $(seq 1 $MAX_HEALTH_RETRIES); do
    
    HTTP_CODE=$(curl -s -o /tmp/health_response.json -w "%{http_code}" \
        "http://${HEALTH_HOST}:${HEALTH_PORT}${HEALTH_ENDPOINT}" || true)
    
    RESPONSE_BODY=""
    
    if [ -f /tmp/health_response.json ]; then
        
        RESPONSE_BODY=$(cat /tmp/health_response.json)
        
    fi 
    
    if [ "$HTTP_CODE" = "200" ] && [[ "$RESPONSE_BODY" == *"\"status\":\"ok\""* ]]; then
        
        echo "[START] Server is ready! (health check passed)"
        
        rm -f /tmp/health_response.json
        
        exit 0
        
    else
        
        echo "[START] Health not yet OK: HTTP=$HTTP_CODE, body='$RESPONSE_BODY' ($attempt/$MAX_HEALTH_RETRIES)"
        
    fi  
    
    sleep $HEALTH_RETRY_DELAY
    
done

rm -f /tmp/health_response.json
  
exit 1
