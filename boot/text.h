#pragma once

extern int txt_debug;
extern int x;
extern int y;
extern int attr;
extern int scroll_disabled;
extern void print_(char * str);
extern void move_cursor(int _x, int _y);
extern void move_cursor_rel(int _x, int _y);
extern void set_attr(int _attr);
extern void print_banner(char * str);
extern void print_hex_(unsigned int value);
extern void clear_(void);

#define print(s) do {if (txt_debug) {print_(s);}} while(0)
#define clear() do {if (txt_debug) {clear_();}} while(0)
#define print_hex(d) do {if (txt_debug) {print_hex_(d);}} while(0)

#ifdef EFI_PLATFORM
extern void print_int_(unsigned int);
#endif
