#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <unistd.h>
#include <stdbool.h>

enum Action {
    NoAction, OpenTerminal, OpenLauncher, QuitWM, CloseWindow, SwitchFocus, Fullscreen,
    SwitchWorkspace, MoveToWorkspace, FocusLeft, FocusDown, FocusUp, FocusRight, ToggleFloat
};
enum { MaxClients = 64, TileGap = 8 };
struct Client {
    Window id;
    int workspace;
    bool floating;
};

static int (*defaultXError)(Display *, XErrorEvent *);

static int windowGone(Display *display, XErrorEvent *error)
{
    if (error->error_code == BadWindow ||
        (error->error_code == BadMatch && error->request_code == X_SetInputFocus)) {
        return 0;
    }
    return defaultXError(display, error);
}

static bool onWorkspace(const struct Client *clients, size_t count, int workspace, Window window)
{
    for (size_t i = 0; i < count; ++i) {
        if (clients[i].id == window && clients[i].workspace == workspace) {
            return true;
        }
    }
    return false;
}

static void focusDirection(Display *display, const struct Client *clients, size_t count,
                           int workspace, enum Action direction, Time time)
{
    Window focused;
    int revertMode;
    XGetInputFocus(display, &focused, &revertMode);
    XWindowAttributes source;
    if (!onWorkspace(clients, count, workspace, focused) ||
        !XGetWindowAttributes(display, focused, &source)) {
        return;
    }

    bool horizontal = direction == FocusLeft || direction == FocusRight;
    bool forward = direction == FocusRight || direction == FocusDown;
    int sourceStart = horizontal ? source.x : source.y;
    int sourceSize = horizontal ? source.width : source.height;
    int sourceCrossCenter = horizontal ? 2 * source.y + source.height : 2 * source.x + source.width;
    Window nearest = None;
    int bestGap = 0;
    int bestOffset = 0;

    for (size_t i = 0; i < count; ++i) {
        Window window = clients[i].id;
        XWindowAttributes target;
        if (clients[i].workspace != workspace || window == focused ||
            !XGetWindowAttributes(display, window, &target) || target.map_state != IsViewable) {
            continue;
        }
        int start = horizontal ? target.x : target.y;
        int size = horizontal ? target.width : target.height;
        int gap = forward ? start - (sourceStart + sourceSize) : sourceStart - (start + size);
        if (gap < 0) {
            continue;
        }
        int crossCenter = horizontal ? 2 * target.y + target.height : 2 * target.x + target.width;
        int offset = abs(crossCenter - sourceCrossCenter);
        if (nearest == None || gap < bestGap || (gap == bestGap && offset < bestOffset)) {
            nearest = window;
            bestGap = gap;
            bestOffset = offset;
        }
    }

    if (nearest != None) {
        XRaiseWindow(display, nearest);
        XSetInputFocus(display, nearest, RevertToPointerRoot, time);
    }
}

static bool isDmenu(Display *display, Window window)
{
    XClassHint hint;
    if (!XGetClassHint(display, window, &hint)) {
        return false;
    }
    bool matches = hint.res_class && strcmp(hint.res_class, "dmenu") == 0;
    XFree(hint.res_name);
    XFree(hint.res_class);
    return matches;
}

static void tileWorkspace(Display *display, Window root, const struct Client *clients,
                          size_t clientCount, int workspace, Window fullscreenWindow,
                          Window menuWindow)
{
    Window windows[MaxClients];
    int count = 0;
    for (size_t i = 0; i < clientCount; ++i) {
        if (clients[i].workspace != workspace ||
            (clients[i].floating && clients[i].id != fullscreenWindow)) {
            continue;
        }
        XWindowAttributes attrs;
        if (XGetWindowAttributes(display, clients[i].id, &attrs) &&
            attrs.map_state == IsViewable) {
            windows[count++] = clients[i].id;
        }
    }

    XWindowAttributes screen;
    if (count == 0 || !XGetWindowAttributes(display, root, &screen)) {
        return;
    }

    int topInset = 0;
    XWindowAttributes menu;
    if (menuWindow != None && XGetWindowAttributes(display, menuWindow, &menu) &&
        menu.map_state == IsViewable && menu.y == 0) {
        topInset = menu.height + 2 * menu.border_width;
    }
    int availableHeight = screen.height - topInset;
    int columns = count == 1 ? 1 : 2;
    int rows = count == 1 ? 1 : count - 1;
    if (screen.width < columns || availableHeight < rows) {
        return;
    }

    int gap = TileGap;
    int maxHorizontalGap = (screen.width - columns) / (columns + 1);
    int maxVerticalGap = (availableHeight - rows) / (rows + 1);
    if (gap > maxHorizontalGap) {
        gap = maxHorizontalGap;
    }
    if (gap > maxVerticalGap) {
        gap = maxVerticalGap;
    }
    int tileWidth = screen.width - (columns + 1) * gap;
    int mainWidth = columns == 1 ? tileWidth : tileWidth / 2;
    int stackHeight = availableHeight - (rows + 1) * gap;

    for (int i = 0; i < count; ++i) {
        if (windows[i] == fullscreenWindow) {
            XMoveResizeWindow(display, windows[i], 0, topInset, screen.width, availableHeight);
            continue;
        }
        int x = i == 0 ? gap : mainWidth + 2 * gap;
        int width = i == 0 ? mainWidth : tileWidth - mainWidth;
        int top = i == 0 ? gap : gap * i + stackHeight * (i - 1) / rows;
        int height = i == 0 ? availableHeight - 2 * gap
                            : stackHeight * i / rows - stackHeight * (i - 1) / rows;
        XSetWindowBorderWidth(display, windows[i], 0);
        XMoveResizeWindow(display, windows[i], x, topInset + top, width, height);
    }
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Bruv turn on xephyr you dumbass, also set ur DISPLAY var?\n");
        return 1;
    }

    Window root = DefaultRootWindow(display);
    signal(SIGCHLD, SIG_IGN);

    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);

    XSync(display, False);
    defaultXError = XSetErrorHandler(windowGone);

    printf("connected, you didnt fuck it up this time.\n");

    struct {
        KeySym symbol;
        KeyCode keycode;
        unsigned int modifiers;
        enum Action action;
        int workspace;
    } bindings[] = {
        {XK_Return, 0, Mod1Mask, OpenTerminal, -1},
        {XK_d, 0, Mod1Mask, OpenLauncher, -1},
        {XK_q, 0, Mod4Mask | ShiftMask, QuitWM, -1},
        {XK_q, 0, Mod1Mask, CloseWindow, -1},
        {XK_Tab, 0, Mod1Mask, SwitchFocus, -1},
        {XK_h, 0, Mod1Mask, FocusLeft, -1},
        {XK_j, 0, Mod1Mask, FocusDown, -1},
        {XK_k, 0, Mod1Mask, FocusUp, -1},
        {XK_l, 0, Mod1Mask, FocusRight, -1},
        {XK_f, 0, Mod1Mask, Fullscreen, -1},
        {XK_space, 0, Mod1Mask, ToggleFloat, -1},
        {XK_1, 0, Mod1Mask, SwitchWorkspace, 0},
        {XK_2, 0, Mod1Mask, SwitchWorkspace, 1},
        {XK_3, 0, Mod1Mask, SwitchWorkspace, 2},
    };
    size_t bindingCount = sizeof bindings / sizeof bindings[0];
    for (size_t i = 0; i < bindingCount; ++i) {
        bindings[i].keycode = XKeysymToKeycode(display, bindings[i].symbol);
        if (bindings[i].keycode != 0) {
            XGrabKey(display, bindings[i].keycode, bindings[i].modifiers,
                     root, False, GrabModeAsync, GrabModeAsync);
        }
    }
    unsigned int numLock = 0;
    KeyCode numLockKey = XKeysymToKeycode(display, XK_Num_Lock);
    XModifierKeymap *modifiers = XGetModifierMapping(display);
    if (modifiers != NULL) {
        for (int i = 0; i < 8 * modifiers->max_keypermod; ++i) {
            if (numLockKey != 0 && modifiers->modifiermap[i] == numLockKey) {
                numLock |= 1u << (i / modifiers->max_keypermod);
            }
        }
        XFreeModifiermap(modifiers);
    }
    unsigned int locks[] = {0, LockMask, numLock, LockMask | numLock};
    for (size_t i = 0; i < sizeof locks / sizeof locks[0]; ++i) {
        XGrabButton(display, Button1, Mod1Mask | locks[i], root, False,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(display, Button3, Mod1Mask | locks[i], root, False,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
    }
    Window dragWindow = None;
    unsigned int dragButton = 0;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    Window fullscreenWindow = None;
    Window menuWindow = None;
    bool workspaceChord = false;
    Window chordWindow = None;
    XWindowAttributes saved;
    int currentWorkspace = 0;
    struct Client clients[MaxClients];
    size_t clientCount = 0;
    // event loop yay
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == ButtonPress &&
            (event.xbutton.button == Button1 || event.xbutton.button == Button3)) {
            if (dragWindow != None) {
                continue;
            }
            Window window = event.xbutton.subwindow;
            for (size_t i = 0; i < clientCount; ++i) {
                XWindowAttributes attrs;
                if (clients[i].id != window || clients[i].workspace != currentWorkspace ||
                    window == fullscreenWindow ||
                    !XGetWindowAttributes(display, window, &attrs) || attrs.map_state != IsViewable) {
                    continue;
                }
                dragWindow = window;
                dragButton = event.xbutton.button;
                if (dragButton == Button3) {
                    dragOffsetX = event.xbutton.x_root - attrs.width;
                    dragOffsetY = event.xbutton.y_root - attrs.height;
                } else {
                    dragOffsetX = event.xbutton.x_root - attrs.x;
                    dragOffsetY = event.xbutton.y_root - attrs.y;
                }
                clients[i].floating = true;
                XRaiseWindow(display, window);
                XSetInputFocus(display, window, RevertToPointerRoot, event.xbutton.time);
                tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
                break;
            }
            continue;
        }
        if (event.type == MotionNotify && dragWindow != None) {
            int x = event.xmotion.x_root - dragOffsetX;
            int y = event.xmotion.y_root - dragOffsetY;
            if (dragButton == Button3) {
                XResizeWindow(display, dragWindow, x < 32 ? 32 : x, y < 32 ? 32 : y);
            } else {
                XMoveWindow(display, dragWindow, x, y);
            }
            continue;
        }
        if (event.type == ButtonRelease && event.xbutton.button == dragButton) {
            dragWindow = None;
            continue;
        }
        if (dragWindow != None &&
            ((event.type == UnmapNotify && event.xunmap.window == dragWindow) ||
             (event.type == DestroyNotify && event.xdestroywindow.window == dragWindow))) {
            dragWindow = None;
        }
        // dmenu bypasses MapRequest, so track its root-level map and unmap events.
        if (event.type == MapNotify && event.xmap.event == root &&
            event.xmap.override_redirect && isDmenu(display, event.xmap.window)) {
            menuWindow = event.xmap.window;
            tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
            continue;
        }
        if (menuWindow != None &&
            ((event.type == UnmapNotify && event.xunmap.window == menuWindow) ||
             (event.type == DestroyNotify && event.xdestroywindow.window == menuWindow))) {
            menuWindow = None;
            tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
            continue;
        }
        // map request
        if (event.type == MapRequest) {
            Window appId = event.xmaprequest.window;
            bool found = false;
            for (size_t i = 0; i < clientCount; ++i) {
                if (clients[i].id == appId) {
                    if (clients[i].workspace != currentWorkspace) {
                        appId = None;
                    }
                    found = true;
                    break;
                }
            }
            if (appId == None) {
                continue;
            }
            if (!found && clientCount < MaxClients) {
                clients[clientCount++] = (struct Client){ .id = appId, .workspace = currentWorkspace };
                XSelectInput(display, appId, EnterWindowMask);
            }
            XMapWindow(display, appId);
            tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
            XSetInputFocus(display, appId, RevertToPointerRoot, CurrentTime);
        }
        if (dragWindow == None && event.type == EnterNotify && event.xcrossing.mode == NotifyNormal &&
            event.xcrossing.detail != NotifyInferior &&
            onWorkspace(clients, clientCount, currentWorkspace, event.xcrossing.window)) {
            XSetInputFocus(display, event.xcrossing.window, RevertToPointerRoot, event.xcrossing.time);
        }
        // destroys closed workspace's appIds
        if (event.type == DestroyNotify) {
            Window destroyed = event.xdestroywindow.window;

            for (size_t i = 0; i < clientCount; ++i) {
                if (clients[i].id == destroyed) {
                    --clientCount;
                    for (; i < clientCount; ++i) {
                        clients[i] = clients[i + 1];
                    }
                    break;
                }
            }
        }
        enum Action action = NoAction;
        // key press
        if (workspaceChord && event.type == KeyRelease) {
            KeySym released = XLookupKeysym(&event.xkey, 0);
            if (released == XK_Alt_L || released == XK_Alt_R) {
                XUngrabKeyboard(display, event.xkey.time);
                workspaceChord = false;
                action = SwitchFocus;
            }
        } else if (event.type == KeyPress) {
            XKeyEvent bindKey = event.xkey;
            int pickedWorkspace = -1;
            for (size_t i = 0; i < bindingCount; ++i) {
                if (bindKey.keycode == bindings[i].keycode &&
                    (bindKey.state & bindings[i].modifiers) == bindings[i].modifiers) {
                    action = bindings[i].action;
                    pickedWorkspace = bindings[i].workspace;
                    break;
                }
            }
            if (workspaceChord) {
                if (action != SwitchWorkspace) {
                    continue;
                }
                XUngrabKeyboard(display, bindKey.time);
                workspaceChord = false;
                action = MoveToWorkspace;
            } else if (action == SwitchFocus &&
                       XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync,
                                     bindKey.time) == GrabSuccess) {
                int revertMode;
                XGetInputFocus(display, &chordWindow, &revertMode);
                workspaceChord = true;
                continue;
            }
            if (action == QuitWM) {
                break;
            }

            switch (action) {
            case FocusLeft:
            case FocusDown:
            case FocusUp:
            case FocusRight:
                focusDirection(display, clients, clientCount, currentWorkspace, action, bindKey.time);
                break;
            case CloseWindow: {
                Window focused;
                int revertMode;
                XGetInputFocus(display, &focused, &revertMode);
                if (focused == None || focused == PointerRoot || focused == root) {
                    continue;
                }
                Atom *protocols;
                int protocolCount;
                if (XGetWMProtocols(display, focused, &protocols, &protocolCount) != 0) {
                    Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
                    for (int i = 0; i < protocolCount; ++i) {
                        if (protocols[i] == deleteWindow) {
                            XEvent message = {0};
                            message.xclient.type = ClientMessage;
                            message.xclient.window = focused;
                            message.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", False);
                            message.xclient.format = 32;
                            message.xclient.data.l[0] = deleteWindow;
                            message.xclient.data.l[1] = bindKey.time;
                            XSendEvent(display, focused, False, NoEventMask, &message);
                            break;
                        }
                    }
                    XFree(protocols);
                }
                break;
            }




            case SwitchWorkspace:
            case MoveToWorkspace: {
                if (pickedWorkspace == currentWorkspace) {
                    break;
                }
                if (action == MoveToWorkspace &&
                    !onWorkspace(clients, clientCount, currentWorkspace, chordWindow)) {
                    break;
                }
                dragWindow = None;
                int previousWorkspace = currentWorkspace;
                currentWorkspace = pickedWorkspace;
                Window nextFocus = PointerRoot;
                for (size_t i = 0; i < clientCount; ++i) {
                    if (action == MoveToWorkspace && clients[i].id == chordWindow) {
                        clients[i].workspace = currentWorkspace;
                    }
                    if (clients[i].workspace == currentWorkspace) {
                        XMapWindow(display, clients[i].id);
                        nextFocus = clients[i].id;
                    }
                }
                tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
                // Keep the old windows visible until the destination is mapped and tiled.
                for (size_t i = 0; i < clientCount; ++i) {
                    if (clients[i].workspace == previousWorkspace) {
                        XUnmapWindow(display, clients[i].id);
                    }
                }
                if (action == MoveToWorkspace) {
                    nextFocus = chordWindow;
                    XRaiseWindow(display, nextFocus);
                    XSync(display, False);
                    XEvent crossing;
                    while (XCheckTypedEvent(display, EnterNotify, &crossing)) {
                        // Mapping and tiling can generate enter events under a stationary pointer.
                    }
                }
                XSetInputFocus(display, nextFocus, RevertToPointerRoot, CurrentTime);
                break;
            }



            case OpenTerminal:
            case OpenLauncher: {
                const char *command = action == OpenTerminal ? "st" : "launcher";
                pid_t child = fork();
                if (child == 0) {
                    signal(SIGCHLD, SIG_DFL);
                    close(ConnectionNumber(display));
                    execlp(command, command, (char *)NULL);
                    perror(command);
                    _exit(1);
                }
                if (child < 0) {
                    perror("fork");
                }
                break;
            }
            case ToggleFloat: {
                Window focused;
                int revertMode;
                XGetInputFocus(display, &focused, &revertMode);
                for (size_t i = 0; i < clientCount; ++i) {
                    if (clients[i].id != focused || clients[i].workspace != currentWorkspace ||
                        focused == fullscreenWindow) {
                        continue;
                    }
                    dragWindow = None;
                    clients[i].floating = !clients[i].floating;
                    if (clients[i].floating) {
                        XRaiseWindow(display, focused);
                    }
                    tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
                    break;
                }
                break;
            }
            case Fullscreen: {
                Window focused;
                int revertMode;
                XGetInputFocus(display, &focused, &revertMode);
                if (focused == None || focused == PointerRoot || focused == root) {
                    break;
                }
                if (focused == fullscreenWindow) {
                    XMoveResizeWindow(display, focused, saved.x, saved.y, saved.width, saved.height);
                    XSetWindowBorderWidth(display, focused, saved.border_width);
                    fullscreenWindow = None;
                    tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
                    break;
                }

                if (fullscreenWindow == None && XGetWindowAttributes(display, focused, &saved)) {
                    dragWindow = None;
                    fullscreenWindow = focused;
                    XSetWindowBorderWidth(display, focused, 0);
                    tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
                    XRaiseWindow(display, focused);
                }
                break;
            }
            default:
                break;
            }
        }

        if (event.type == UnmapNotify && event.xunmap.window == fullscreenWindow) {
            XMoveResizeWindow(display, fullscreenWindow, saved.x, saved.y, saved.width, saved.height);
            XSetWindowBorderWidth(display, fullscreenWindow, saved.border_width);
            fullscreenWindow = None;
        }
        if ((event.type == UnmapNotify && event.xunmap.event == root) ||
            (event.type == ConfigureNotify &&
             (event.xconfigure.window == root || event.xconfigure.window == menuWindow))) {
            tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow, menuWindow);
        }

        // focus another window when the current one disappears
        if (action == SwitchFocus || (event.type == UnmapNotify && event.xunmap.event == root)) {
            Window focused;
            int revertMode;
            XGetInputFocus(display, &focused, &revertMode);
            if (action != SwitchFocus && focused != None && focused != PointerRoot && focused != root) {
                continue;
            }

            Window returnedRoot;
            Window parent;
            Window *children;
            unsigned int childCount;
            if (XQueryTree(display, root, &returnedRoot, &parent, &children, &childCount) && children != NULL) {
                for (unsigned int i = childCount; i > 0; --i) {
                    Window countIds = children[i - 1];
                    if (countIds == focused ||
                        !onWorkspace(clients, clientCount, currentWorkspace, countIds)) {
                        continue;
                    }

                    XWindowAttributes attrs;
                    if (XGetWindowAttributes(display, countIds, &attrs) == 0) {
                        continue;
                    }
                    if (attrs.map_state != IsViewable || attrs.override_redirect) {
                        continue;
                    }
                    XRaiseWindow(display, countIds);
                    XSetInputFocus(display, countIds, RevertToPointerRoot, CurrentTime);
                    break;
                }
                XFree(children);
            }
        }
        if (event.type == ConfigureRequest) {
            XConfigureRequestEvent request = event.xconfigurerequest;
            unsigned int mask = request.value_mask;
            for (size_t i = 0; i < clientCount; ++i) {
                if (clients[i].id == request.window) {
                    if (!clients[i].floating || request.window == fullscreenWindow) {
                        mask &= CWStackMode | CWSibling;
                    }
                    break;
                }
            }
            XWindowChanges changes;
            changes.x = request.x;
            changes.y = request.y;
            changes.width = request.width;
            changes.height = request.height;
            changes.border_width = request.border_width;
            changes.sibling = request.above;
            changes.stack_mode = request.detail;
            if (mask != 0) {
                XConfigureWindow(display, request.window, mask, &changes);
            }
            if (mask != request.value_mask) {
                XWindowAttributes attrs;
                if (XGetWindowAttributes(display, request.window, &attrs)) {
                    XEvent reply = {0};
                    reply.xconfigure.type = ConfigureNotify;
                    reply.xconfigure.event = request.window;
                    reply.xconfigure.window = request.window;
                    reply.xconfigure.x = attrs.x;
                    reply.xconfigure.y = attrs.y;
                    reply.xconfigure.width = attrs.width;
                    reply.xconfigure.height = attrs.height;
                    reply.xconfigure.border_width = attrs.border_width;
                    XSendEvent(display, request.window, False, StructureNotifyMask, &reply);
                }
            }
        }

    }

    XCloseDisplay(display);
    return 0;
}
