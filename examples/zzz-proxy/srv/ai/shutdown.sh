#!/bin/bash

source ../goto.sh

./stop.sh
: retry
echo "shutdown..."
ssh -x gyula@monster "sudo shutdown -h now" || {
    echo "failed. retry..."
    sleep 1
    goto retry
}
