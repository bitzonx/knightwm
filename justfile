cc := "clang"
cflags := "-std=c11 -Wall -Wextra -g"
libs := "-lX11"

build:
    {{cc}} {{cflags}} knightwm.c -o knightwm {{libs}}

[positional-arguments]
run display=":1": build
    #!/bin/sh
    export DISPLAY="$1"
    if command -v picom >/dev/null 2>&1; then
        picom --config ./picom.conf &
        picom_pid=$!
        trap 'kill "$picom_pid" 2>/dev/null; wait "$picom_pid" 2>/dev/null' EXIT
        trap 'exit 130' INT
        trap 'exit 143' TERM
    fi
    ./knightwm

clean:
    rm -f knightwm
