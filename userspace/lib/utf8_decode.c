/* utf8_decode.c */

/* 2009-02-13 */

/*
Copyright (c) 2005 JSON.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

The Software shall be used for Good, not Evil.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "utf8_decode.h"

/*
    Very Strict UTF-8 Decoder

    UTF-8 is a multibyte character encoding of Unicode. A character can be
    represented by 1-4 bytes. The bit pattern of the first byte indicates the
    number of continuation bytes.

    Most UTF-8 decoders tend to be lenient, attempting to recover as much
    information as possible, even from badly encoded input. This UTF-8
    decoder is not lenient. It will reject input which does not include
    proper continuation bytes. It will reject aliases (or suboptimal
    codings). It will reject surrogates. (Surrogate encoding should only be
    used with UTF-16.)

    Code     Contination Minimum Maximum
    0xxxxxxx           0       0     127
    10xxxxxx       error
    110xxxxx           1     128    2047
    1110xxxx           2    2048   65535 excluding 55296 - 57343
    11110xxx           3   65536 1114111
    11111xxx       error
*/


static int  the_index = 0;
static int  the_length = 0;
static int  the_char = 0;
static int  the_byte = 0;
static char* the_input;


/*
    Get the next byte. It returns UTF8_END if there are no more bytes.
*/
static int
get()
{
    int c;
    if (the_index >= the_length) {
        return UTF8_END;
    }
    c = the_input[the_index] & 0xFF;
    the_index += 1;
    return c;
}


/*
    Get the 6-bit payload of the next continuation byte.
    Return UTF8_ERROR if it is not a contination byte.
*/
static int
cont()
{
    int c = get();
    return ((c & 0xC0) == 0x80) ? (c & 0x3F) : UTF8_ERROR;
}


/*
    Initialize the UTF-8 decoder. The decoder is not reentrant,
*/
void
utf8_decode_init(char p[], int length)
{
    the_index = 0;
    the_input = p;
    the_length = length;
    the_char = 0;
    the_byte = 0;
}


/*
    Get the current byte offset. This is generally used in error reporting.
*/
int
utf8_decode_at_byte()
{
    return the_byte;
}


/*
    Get the current character offset. This is generally used in error reporting.
    The character offset matches the byte offset if the text is strictly ASCII.
*/
int
utf8_decode_at_character()
{
    return the_char > 0 ? the_char - 1 : 0;
}


/*
    Extract the next character.
    Returns: the character (between 0 and 1114111)
         or  UTF8_END   (the end)
         or  UTF8_ERROR (error)
*/
int
utf8_decode_next()
{
    int c;  /* the first byte of the character */
    int r;  /* the result */

    if (the_index >= the_length) {
        return the_index == the_length ? UTF8_END : UTF8_ERROR;
    }
    the_byte = the_index;
    the_char += 1;
    c = get();
/*
    Zero continuation (0 to 127)
*/
    if ((c & 0x80) == 0) {
        return c;
    }
/*
    One contination (128 to 2047)
*/
    if ((c & 0xE0) == 0xC0) {
        int c1 = cont();
        if (c1 < 0) {
            return UTF8_ERROR;
        }
        r = ((c & 0x1F) << 6) | c1;
        return r >= 128 ? r : UTF8_ERROR;
    }
/*
    Two continuation (2048 to 55295 and 57344 to 65535)
*/
    if ((c & 0xF0) == 0xE0) {
        int c1 = cont();
        int c2 = cont();
        if (c1 < 0 || c2 < 0) {
            return UTF8_ERROR;
        }
        r = ((c & 0x0F) << 12) | (c1 << 6) | c2;
        return r >= 2048 && (r < 55296 || r > 57343) ? r : UTF8_ERROR;
    }
/*
    Three continuation (65536 to 1114111)
*/
    if ((c & 0xF8) == 0xF0) {
        int c1 = cont();
        int c2 = cont();
        int c3 = cont();
        if (c1 < 0 || c2 < 0 || c3 < 0) {
            return UTF8_ERROR;
        }
        r = ((c & 0x0F) << 18) | (c1 << 12) | (c2 << 6) | c3;
        return r >= 65536 && r <= 1114111 ? r : UTF8_ERROR;
    }
    return UTF8_ERROR;
}