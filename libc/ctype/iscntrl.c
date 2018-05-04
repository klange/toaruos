int iscntrl(int c) {
    return ((c >= 0 && c <= 0x1f) || (c == 0x7f));
}
