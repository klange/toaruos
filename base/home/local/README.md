# Welcome to ToaruOS!

ToaruOS provides a familiar Unix-like environment, but please be
aware that the shell is incomplete and does not implement all Unix
shell features. For help with the shell's syntax and built-in
functions, run `help`. For a list of available commands, press Tab
twice. Tab completion is available for both commands and file names.

To edit files, try using `bim` - a vi-like editor with syntax
highlighting, line and character selection, history stack, tabs, and more.

To install packages, use the `msk` tool. You can install a GCC/binutils
toolchain with:

    sudo msk install build-essential

Or you can install Python with:

    sudo msk install python

The password for the default user (`local`) is `local`.

ToaruOS's compositing window server includes many common keybindings:
- Hold Alt to drag windows.
- Super (Win) combined with the arrow keys will "grid" windows to the
  sides or top and bottom of the screen. Combine with Ctrl and Shift
  for quarter-sized gridding.
- Alt-F10 maximized and unmaximizes windows.
- Alt-F4 closes windows.

(If this file is too long to view in one screenful in your terminal,
 you can open it with `bim README`)
