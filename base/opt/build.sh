#!/bin/sh

export CRTBEG="/lib/crt0.o /lib/crti.o /usr/lib/crtbegin.o"
export CRTEND="/usr/lib/crtend.o /lib/crtn.o"

/usr/i686-pc-toaru/bin/as -o /tmp/asm-demo.o asm-demo.s
/usr/i686-pc-toaru/bin/ld -o /tmp/asm-demo $CRTBEG -lc /tmp/asm-demo.o $CRTEND
