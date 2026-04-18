#!/bin/bash

source ../goto.sh

: retry
ssh -x gyula@monster "pkill -f llama.cpp" || {
    echo "pkill failed, retry..."
    sleep 1
    goto retry
}
