#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <unistd.h>
#include <stdbool.h>

enum Action { NoAction, OpenTerminal, QuitWM, CloseWindow, SwitchFocus, Fullscreen, SwitchWorkspace };
enum { MaxClients = 64 };
struct Client {
    Window id;
    int workspace;
};

static int (*defaultXError)(Display *, XErrorEvent *);

static int windowGone(Display *display, XErrorEvent *error)
{
    if (error->error_code == BadWindow) {
        return 0;
    }
    return defaultXError(display, error);
}

static void tileWorkspace(Display *display, Window root, const struct Client *clients,
                          size_t clientCount, int workspace, Window fullscreenWindow)
{
    Window windows[MaxClients];
    int count = 0;
    for (size_t i = 0; i < clientCount; ++i) {
        if (clients[i].workspace != workspace) {
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

    int mainWidth = count == 1 ? screen.width : screen.width / 2;
    if (mainWidth == 0 || screen.height < count - 1) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (windows[i] == fullscreenWindow) {
            XMoveResizeWindow(display, windows[i], 0, 0, screen.width, screen.height);
            continue;
        }
        int x = i == 0 ? 0 : mainWidth;
        int width = i == 0 ? mainWidth : screen.width - mainWidth;
        int top = i == 0 ? 0 : screen.height * (i - 1) / (count - 1);
        int bottom = i == 0 ? screen.height : screen.height * i / (count - 1);
        XSetWindowBorderWidth(display, windows[i], 0);
        XMoveResizeWindow(display, windows[i], x, top, width, bottom - top);
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

    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);

    XSync(display, False);
    defaultXError = XSetErrorHandler(windowGone);
    unsigned long white = WhitePixel(display, DefaultScreen(display));
    XSetWindowBackground(display, root, white);
    XClearWindow(display, root);

    printf("connected, you didnt fuck it up this time.\n");

    struct {
        KeySym symbol;
        KeyCode keycode;
        unsigned int modifiers;
        enum Action action;
        int workspace;
    } bindings[] = {
        {XK_Return, 0, Mod1Mask, OpenTerminal, -1},
        {XK_q, 0, Mod4Mask | ShiftMask, QuitWM, -1},
        {XK_q, 0, Mod1Mask, CloseWindow, -1},
        {XK_Tab, 0, Mod1Mask, SwitchFocus, -1},
        {XK_f, 0, Mod1Mask, Fullscreen, -1},
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
    Window fullscreenWindow = None;
    XWindowAttributes saved;
    int currentWorkspace = 0;
    struct Client clients[MaxClients];
    size_t clientCount = 0;
    // event loop yay
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
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
                clients[clientCount].id = appId;
                clients[clientCount].workspace = currentWorkspace;
                clientCount += 1;
            }
            XMapWindow(display, appId);
            tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow);
            XSetInputFocus(display, appId, RevertToPointerRoot, CurrentTime);
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
        if (event.type == KeyPress) {
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
            if (action == QuitWM) {
                break;
            }

            switch (action) {
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




            case SwitchWorkspace: {
                if (pickedWorkspace == currentWorkspace) {
                    break;
                }
                for (size_t i = 0; i < clientCount; ++i) {
                    if (clients[i].workspace == currentWorkspace) {
                        XUnmapWindow(display, clients[i].id);
                    }
                }
                currentWorkspace = pickedWorkspace;
                Window nextFocus = PointerRoot;
                for (size_t i = 0; i < clientCount; ++i) {
                    if (clients[i].workspace == currentWorkspace) {
                        XMapWindow(display, clients[i].id);
                        nextFocus = clients[i].id;
                    }
                }
                tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow);
                XSetInputFocus(display, nextFocus, RevertToPointerRoot, CurrentTime);
                break;
            }



            case OpenTerminal: {
                pid_t pst = fork();
                if (pst == 0) {
                    execlp("st", "st", (char *)NULL);
                    perror("st");
                    _exit(1);
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
                    tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow);
                    break;
                }

                XWindowAttributes screen;
                if (fullscreenWindow == None &&
                    XGetWindowAttributes(display, focused, &saved) &&
                    XGetWindowAttributes(display, root, &screen)) {
                    fullscreenWindow = focused;
                    XSetWindowBorderWidth(display, focused, 0);
                    XMoveResizeWindow(display, focused, 0, 0, screen.width, screen.height);
                    XRaiseWindow(display, focused);
                }
                break;
            }
            default:
                break;
            }
        }

        if (event.type == UnmapNotify && event.xunmap.window == fullscreenWindow) {
            fullscreenWindow = None;
        }
        if ((event.type == UnmapNotify && event.xunmap.event == root) ||
            (event.type == ConfigureNotify && event.xconfigure.window == root)) {
            tileWorkspace(display, root, clients, clientCount, currentWorkspace, fullscreenWindow);
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
                    if (countIds == focused) {
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
                    mask &= CWStackMode | CWSibling;
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
