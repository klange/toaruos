#!/usr/bin/python3.6
"""
Toaru OS UI theming.
"""

panel_widget_foreground = 0xFFE6E6E6
panel_widget_hilight = 0xFF8EDBFF

menu_entry_text = 0xFF000000

panel_window_shadow = (0xFF000000, 2, 1, 1, 3.0)
panel_window_gradient_top = (0.0,72/255,167/255,255/255,0.7)
panel_window_gradient_low = (1.0,72/255,167/255,255/255,0.0)

panel_window_divider_top = (0.1,1,1,1,0.0)
panel_window_divider_mid = (0.5,1,1,1,1.0)
panel_window_divider_low = (0.9,1,1,1,0.0)

desktop_icon_hilight = (0x8E/0xFF,0xD8/0xFF,1,0.3)
desktop_icon_shadow = (0xFF000000, 2, 1, 1, 3.0)
desktop_icon_text = 0xFFFFFFFF

alt_tab_text = 0xFFE6E6E6
alt_tab_background = (0,0,0,0.7)
alt_tab_extra_text = '0x888888'


def as_rgb_tuple(color):
    r = ((color & 0xFF0000) >> 16) / 0xFF
    g = ((color & 0xFF00) >> 8) / 0xFF
    b = ((color & 0xFF)) / 0xFF
    return (r,g,b)

def as_rgba_tuple(color):
    a = ((color & 0xFF000000) >> 24) / 0xFF
    r = ((color & 0xFF0000) >> 16) / 0xFF
    g = ((color & 0xFF00) >> 8) / 0xFF
    b = ((color & 0xFF)) / 0xFF
    return (r,g,b,a)

if __name__ == '__main__':
    print("...")
