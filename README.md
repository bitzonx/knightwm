# tinywm

A small X11 window manager written in C with Xlib. It provides three workspaces, automatic tiling, and keyboard shortcuts to launch, focus, close, and fullscreen windows.

## Setup on Arch Linux

Install the build dependencies and Xephyr, which runs a nested X11 session for testing.

```sh
sudo pacman -S --needed clang just libx11 xorg-server-xephyr
```

The terminal shortcut launches [suckless st](https://st.suckless.org/). Install it separately and make sure `st` is on your `PATH`. You can also launch another X11 app from your host terminal by setting its `DISPLAY` to Xephyr's display.

From the repository directory, build the WM.

```sh
just build
```

Open two host terminals. In the first, start Xephyr and leave it running.

```sh
Xephyr :1 -screen 1024x768
```

Once the Xephyr window appears, run this from the repository directory in the second terminal.

```sh
DISPLAY=:1 ./wm
```

Click inside Xephyr and press **Alt+Enter** to open a terminal. If display `:1` is occupied, choose another display number and use it in both commands. If the host desktop intercepts a shortcut, free that shortcut in the host or change it in the binding table in `wm.c`.

## Key bindings

| Shortcut | Action |
| --- | --- |
| Alt+Enter | Launch `st` |
| Alt+Q | Ask the focused window to close |
| Alt+Tab | Focus another window |
| Alt+F | Toggle fullscreen for the focused window |
| Alt+1 / Alt+2 / Alt+3 | Switch workspaces |
| Super+Shift+Q | Quit tinywm |

One window fills the workspace. Two split it side by side. With more windows, the first gets the left half and the others stack on the right.

## Optional wallpaper

Install `feh` with `sudo pacman -S --needed feh`. Run it from a host terminal after starting tinywm.

```sh
DISPLAY=:1 feh --bg-fill /path/to/wallpaper.jpg
```

Switch to an empty workspace to see the wallpaper.
