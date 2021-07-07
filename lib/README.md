# ToaruOS System Libraries

These are the core system libraries of ToaruOS. Where functionality isn't expected in the C standard library, these provide additional features that are shared by multiple ToaruOS applications.

## `toaru_auth`

Provides password validation and login helper methods. Exists primarily because `libc` doesn't have these things and there are multiple places where logins are checked (`login`, `glogin`, `sudo`, `gsudo`...).

## `toaru_button`

Renderer for button widgets. Not really a widget library at the moment.

## `toaru_confreader`

Implements a basic INI parser for use with configuration files.

## `toaru_decorations`

Client-side decoration library for the compositor. Supports pluggable decoration themes through additional libraries, which are named as `libtoaru_decor-...`.

## `toaru_graphics`

General-purpose 2D drawing and pixel-pushing library. Provides sprite blitting, rotation, scaling, etc.

## `toaru_hashmap`

Generic hashmap implementation. Also used by the kernel.

## `toaru_iconcache`

Convenience library for loading icons at specific sizes.

## `toaru_inflate`

Decompression library for DEFLATE payloads.

## `toaru_jpeg`

Minimal, incomplete JPEG decoder. Mostly used for providing wallpapers. Doesn't support most JPEG features.

## `toaru_kbd`

Keyboard scancode parser.

## `toaru_list`

Generic expandable linked list implementation.

## `toaru_markup`

XML-like syntax parser.

## `toaru_menu`

Menu widget library. Used for the "Applications" menu, context menus, etc.

## `toaru_pex`

Userspace library for using the ToaruOS "packetfs" subsystem, which provides packet-based IPC.

## `toaru_png`

Decoder for Portable Network Graphics images.

## `toaru_rline`

Rich line editor for terminal applications, with support for tab completion and syntax highlighting.

## `toaru_termemu`

Terminal ANSI escape processor.

## `toaru_text`

TrueType font parser and text renderer.

## `toaru_tree`

Generic tree implementation. Also used by the kernel.

## `toaru_yutani`

Compositor client library, used to build GUI applications.

