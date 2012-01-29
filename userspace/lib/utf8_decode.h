/* utf8_decode.h */

#define UTF8_END   -1
#define UTF8_ERROR -2

extern int  utf8_decode_at_byte();
extern int  utf8_decode_at_character();
extern void utf8_decode_init(char p[], int length);
extern int  utf8_decode_next();

