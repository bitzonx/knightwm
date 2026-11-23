# knightwm

A small X11 window manager written in C with Xlib. It provides three workspaces, automatic tiling, and keyboard shortcuts to launch, focus, close, and fullscreen windows.

## Setup on Arch Linux

Install the build dependencies and Xephyr, which runs a nested X11 session for testing.

```sh
sudo pacman -S --needed clang just libx11 xorg-server-xephyr picom dmenu
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
just run
```

Click inside Xephyr and press **Alt+Enter** to open a terminal. Hover over a managed window to give it keyboard focus. If display `:1` is occupied, start Xephyr on another display and pass it to the launcher, for example `just run :2`. If the host desktop intercepts a shortcut, free that shortcut in the host or change it in the binding table in `knightwm.c`.

`just run` starts Picom when installed, using the XRender backend and fades configured in `picom.conf`. It stops Picom when knightwm exits. To start knightwm on its own, run `DISPLAY=:1 ./knightwm`.

## Key bindings

| Shortcut | Action |
| --- | --- |
| Alt+Enter | Launch `st` |
| Alt+D | Open the `dmenu_run` application launcher |
| Alt+Q | Ask the focused window to close |
| Alt+Tab, then release Alt | Focus another window |
| Alt+H / Alt+J / Alt+K / Alt+L | Focus the nearest tile to the left / down / up / right |
| Alt+F | Toggle fullscreen for the focused window |
| Alt+Space | Toggle floating for the focused window |
| Alt+left mouse drag | Float and move a window |
| Alt+right mouse drag | Float and resize a window from its bottom-right corner |
| Alt+1 / Alt+2 / Alt+3 | Switch workspaces |
| Hold Alt, press Tab, then press 1 / 2 / 3 | Move the focused window to that workspace and follow it |
| Super+Shift+Q | Quit knightwm |

One window takes the whole tiling area. Two split it side by side. With more windows, the first gets the left half and the others stack on the right. Tiles have an 8-pixel gap between them and around the screen edges. Change `TileGap` in `knightwm.c` and rebuild to adjust the spacing. While dmenu is open, windows shrink to leave room for it at the top. Fullscreen windows fill the display again when dmenu closes.

Floating windows keep their position when switching workspaces and leave the tiling area available to tiled windows. Press Alt+Space again to return the focused window to the layout.

## Optional wallpaper

Install `feh` with `sudo pacman -S --needed feh`. Run it from a host terminal after starting knightwm.

```sh
DISPLAY=:1 feh --bg-fill /path/to/wallpaper.jpg
```

Switch to an empty workspace to see the wallpaper.
