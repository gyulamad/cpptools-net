#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../goto.sh"

# : retry
ssh -x gyula@monster "pkill -f llama.cpp" || {
    echo "pkill failed, process already killed."
    # echo "pkill failed, retry..."
    # sleep 1
    # goto retry
}
