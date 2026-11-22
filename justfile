cc := "clang"
cflags := "-std=c11 -Wall -Wextra -g"
libs := "-lX11"

build:
    {{cc}} {{cflags}} wm.c -o wm {{libs}}

run: build
    DISPLAY=:1 ./wm

clean:
    rm -f wm
