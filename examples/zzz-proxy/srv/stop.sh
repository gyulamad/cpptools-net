#!/bin/bash

set -euo pipefail

SSH_USER="gyula"
SSH_TARGET="monster"
SHUTDOWN_COMMAND="sudo shutdown -h now"

TARGET_HOST="$SSH_TARGET"
TARGET_PORT="8080"
HEALTH_ENDPOINT="/v1/health"
MAX_RETRIES=12       # Maximum number of retry attempts (60 seconds total)
RETRY_INTERVAL=5     # Seconds between retries

echo "[STOP] Initiating graceful shutdown..."

ssh "$SSH_USER@$SSH_TARGET" "$SHUTDOWN_COMMAND"

echo "[STOP] Waiting for server to shut down..."

for attempt in $(seq 1 $MAX_RETRIES); do
    
    echo "[STOP] Checking health ($attempt/$MAX_RETRIES)..."
    
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
        "http://${TARGET_HOST}:${TARGET_PORT}${HEALTH_ENDPOINT}" || true)
    
    if [ "$HTTP_CODE" != "200" ]; then
        
        echo "[STOP] Server has stopped responding."
        
        sleep 15
        
        exit 0
        
    fi
    
    sleep $RETRY_INTERVAL
    
done

echo "[WARN] Server still responding after ${MAX_RETRIES} attempts."
exit 0