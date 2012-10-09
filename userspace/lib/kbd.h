#ifndef KBD_H
#define KBD_H

#define KBD_NORMAL 0
#define KBD_ESC_A  1
#define KBD_ESC_B  2
#define KBD_FUNC   3

#define KEY_NONE        0
#define KEY_BACKSPACE   8
#define KEY_CTRL_C      3
#define KEY_CTRL_L      12
#define KEY_CTRL_R      18
#define KEY_ESCAPE      27
#define KEY_NORMAL_MAX  256
#define KEY_ARROW_UP    257
#define KEY_ARROW_DOWN  258
#define KEY_ARROW_RIGHT 259
#define KEY_ARROW_LEFT  260
#define KEY_BAD_STATE   -1

typedef unsigned int kbd_key_t;

kbd_key_t kbd_key(unsigned char c);
int       kbd_state;

#endif
