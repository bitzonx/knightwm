cc := "clang"
cflags := "-std=c11 -Wall -Wextra -g"
libs := "-lX11"

build:
    {{cc}} {{cflags}} wm.c -o knightwm {{libs}}

run: build
    DISPLAY=:1 ./knightwm

clean:
    rm -f knightwm
