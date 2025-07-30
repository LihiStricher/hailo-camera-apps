#!/bin/bash

first="/home/root/apps/s1_demo/person_detection"
second="/home/root/apps/s1_demo/fire_detection"

current=""
pid=""

run_exec() {
    if [ "$1" = "first" ]; then
        echo "Running FIRST executable..."
        $first &
        pid=$!
        current="first"
    else
        echo "Running SECOND executable..."
        $second &
        pid=$!
        current="second"
    fi
}

kill_current() {
    if [ -n "$pid" ]; then
        echo "Killing PID $pid"
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
        pid=""
    fi
}

# Start with the first executable
run_exec "first"

while true; do
    echo "Press any key to toggle, ESC to exit..."
    read -rsn1 key

    if [[ $key == $'\e' ]]; then
        echo "Exiting..."
        kill_current
        break
    fi

    kill_current
    if [ "$current" = "first" ]; then
        run_exec "second"
    else
        run_exec "first"
    fi
done
