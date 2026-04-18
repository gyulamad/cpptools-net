#!/bin/bash

source ../goto.sh

./wakeup.sh
: start
ssh -x gyula@monster "./llamacpp-server.sh MiniMax-M2.5 100000" || {
    echo "ssh to start llm failed, retry..."
    sleep 1
    goto start
}
