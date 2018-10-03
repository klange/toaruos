import yutani
import menu_bar

def handle_event(msg):
    if msg.type == yutani.Message.MSG_SESSION_END:
        msg.free()
        return False
    elif msg.type == yutani.Message.MSG_WINDOW_CLOSE:
        # TODO should actually send a close signal to the window
        msg.free()
        return False
    elif yutani.yutani_ctx.process_menus(msg):
        return True
    elif msg.type == yutani.Message.MSG_KEY_EVENT:
        if msg.wid in yutani.yutani_windows:
            yutani.yutani_windows[msg.wid].keyboard_event(msg)
    elif msg.type == yutani.Message.MSG_WINDOW_FOCUS_CHANGE:
        if msg.wid in yutani.yutani_windows:
            window = yutani.yutani_windows[msg.wid]
            if msg.wid in menu_bar.menu_windows:
                if msg.focused == 0:
                    window.leave_menu()
                    if window.root and not window.root.menus and window.root.focused:
                        window.root.focused = 0
                        window.root.draw()
            elif msg.focused == 0 and 'menus' in dir(window) and window.menus:
                window.focused = 1
                window.draw()
            else:
                if 'focus_changed' in dir(window):
                    window.focus_changed(msg)
                window.focused = msg.focused
                window.draw()
    elif msg.type == yutani.Message.MSG_RESIZE_OFFER:
        if msg.wid in yutani.yutani_windows:
            yutani.yutani_windows[msg.wid].finish_resize(msg)
    elif msg.type == yutani.Message.MSG_WINDOW_MOVE:
        if msg.wid in yutani.yutani_windows:
            window = yutani.yutani_windows[msg.wid]
            if 'window_moved' in dir(window):
                window.window_moved(msg)
    elif msg.type == yutani.Message.MSG_WINDOW_MOUSE_EVENT:
        if msg.wid in yutani.yutani_windows:
            window = yutani.yutani_windows[msg.wid]
            if msg.wid in menu_bar.menu_windows:
                if window.root:
                    if msg.new_x >= 0 and msg.new_x < window.width and msg.new_y >= 0 and msg.new_y < window.height:
                        window.root.hovered_menu = window
                    else:
                        window.root.hovered_menu = None
                window.mouse_action(msg)
            elif 'mouse_event' in dir(window):
                window.mouse_event(msg)
    msg.free()
    return True


def mainloop():
    status = True
    while status:
        # Poll for events.
        msg = yutani.yutani_ctx.poll()
        status = handle_event(msg)
