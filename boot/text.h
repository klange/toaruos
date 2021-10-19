#pragma once

extern int txt_debug, x, y, attr;

void move_cursor(int _x, int _y);
void set_attr(int _attr);
void print_(char * str);
void print_hex_(unsigned int value);
void print_int_(unsigned int value);
void clear_();

void print_banner(char * str);

#define print(s) do {if (txt_debug) {print_(s);}} while(0)
#define clear() do {if (txt_debug) {clear_();}} while(0)
#define print_hex(d) do {if (txt_debug) {print_hex_(d);}} while(0)

