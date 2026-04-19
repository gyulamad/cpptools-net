#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../goto.sh"

"$SCRIPT_DIR/stop.sh"
: retry
echo "shutdown..."
ssh -x gyula@monster "sudo shutdown -h now" || {
    echo "failed. retry..."
    sleep 1
    goto retry
}
