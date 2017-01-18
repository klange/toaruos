"""
Library for caching Cairo surfaces for icons by name and size.
"""
import os
import cairo

icon_directories = {
    16: [
        "/usr/share/icons/16",
        "/usr/share/icons/external/16",
        "/usr/share/icons/24",
        "/usr/share/icons/external/24",
        "/usr/share/icons/48",
        "/usr/share/icons/external/48",
        "/usr/share/icons",
        "/usr/share/icons/external",
    ],
    24: [
        "/usr/share/icons/24",
        "/usr/share/icons/external/24",
        "/usr/share/icons/48",
        "/usr/share/icons/external/48",
        "/usr/share/icons",
        "/usr/share/icons/external",
    ],
    48: [
        "/usr/share/icons/48",
        "/usr/share/icons/external/48",
        "/usr/share/icons",
        "/usr/share/icons/external",
        "/usr/share/icons/24",
        "/usr/share/icons/external/24",
    ],
}

icon_cache = {24:{},48:{},16:{}}
def get_icon(name,size=24):
    """Find an icon in the icon cache or fetch it if possible."""
    if not name:
        return get_icon("applications-generic",size)

    if not name in icon_cache[size]:
        for directory in icon_directories[size]:
            path = f"{directory}/{name}.png"
            if os.access(path,os.R_OK):
                icon = cairo.ImageSurface.create_from_png(f"{directory}/{name}.png")
                icon_cache[size][name] = icon
                return icon
        return get_icon("applications-generic",size)
    else:
        return icon_cache[size][name]

