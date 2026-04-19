#!/bin/bash

# =============================================
# Llama Server Health Check Script
# Returns 0 (success) if server is healthy, non-zero otherwise
# =============================================

# Configuration - change these as needed
HOST="${LLAMA_HOST:-monster}"
PORT="${LLAMA_PORT:-8080}"
ENDPOINT="${LLAMA_HEALTH_ENDPOINT:-/health}"
TIMEOUT="${LLAMA_TIMEOUT:-5}"          # seconds

URL="http://${HOST}:${PORT}${ENDPOINT}"

# Perform health check
response=$(curl -s -m "$TIMEOUT" -w "%{http_code}" "$URL")
http_code="${response: -3}"           # last 3 characters = HTTP code
body="${response:0:-3}"               # everything before the code

if [ "$http_code" -eq 200 ]; then
    # Check for {"status": "ok"} in body (recommended by llama.cpp)
    if echo "$body" | grep -q '"status"[[:space:]]*:[[:space:]]*"ok"'; then
        echo "✅ Llama server is healthy"
        exit 0
    else
        echo "⚠️  Server returned 200 but status is not 'ok'"
        exit 1
    fi
elif [ "$http_code" -eq 503 ]; then
    echo "⏳ Server is still loading the model (503)"
    exit 1
else
    echo "❌ Llama server unhealthy - HTTP $http_code"
    exit 1
fi