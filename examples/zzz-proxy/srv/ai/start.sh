#!/bin/bash

# Get the directory where THIS script is located (handles symlinks)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source goto from same srv directory (go up one level from ai/)
source "$SCRIPT_DIR/../goto.sh"

"$SCRIPT_DIR/wakeup.sh"
: start
ssh -x gyula@monster "echo LOADED" || {
    echo "ssh to start llm failed, retry..."
    sleep 1
    goto start
}

ssh -x gyula@monster "./llamacpp-server.sh MiniMax-M2.5 40000 &" &
