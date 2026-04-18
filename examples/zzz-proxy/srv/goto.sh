#!/bin/bash

goto() {
    label=$1
    cmd=$(sed -n "/^:[[:blank:]]*$label[[:blank:]]*$/{:a;n;p;ba};" "$0" | grep -v '^:[[:blank:]]')
    eval "$cmd"
    exit
}
