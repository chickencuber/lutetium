#!/bin/bash

cleanup() {
    rm -f ./test.o ./test
}

trap cleanup EXIT

if [[ -z "$1" ]]; then
    for file in ./examples/*.lut; do
        name="${file##*/}"
        name="${name%.lut}"
        echo "$name"
    done
    exit
fi

target="run"
dump=""
if [[ "$2" == "dev" ]]; then
    target="run-dev"
    dump="-a"
fi

export RUN_ARGS="./examples/$1.lut -o test.o $dump"

ninja "$target" || exit

echo "--------------------"
echo "linking"
echo "--------------------"

gcc test.o -o test -lraylib || exit

echo "--------------------"
echo "running"
echo "--------------------"

./test
echo "exited with: $?"
