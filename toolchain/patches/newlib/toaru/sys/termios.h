#ifndef _TERMIOS_H
#define _TERMIOS_H

/* Technically part of ioctl */
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

typedef unsigned int tcflag_t;
typedef unsigned int speed_t;
typedef unsigned char cc_t;

/* reserving 0 for particular reason */
#define VEOF     1 /* ^D (end of file) */
#define VEOL     2 /* NULL (end of line) */
#define VERASE   3 /* ^H (backspace/del) */
#define VINTR    4 /* ^C (interrupt) */
#define VKILL    5 /* ^U (erase input buffer) */
#define VMIN     6 /* minimum number of characters for non-canonical read */
#define VQUIT    7 /* ^\ send SIGQUIT */
#define VSTART   8 /* ^Q restart STOPped input */
#define VSTOP    9 /* ^S stop input */
#define VSUSP   10 /* ^Z suspend foreground applicatioan (send SIGTSTP) */
#define VTIME   11 /* Timeout for non-canonical read, deciseconds */

/* flags for input modes */
#define BRKINT  0000001
#define ICRNL   0000002
#define IGNBRK  0000004
#define IGNCR   0000010
#define IGNPAR  0000020
#define INLCR   0000040
#define INPCK   0000100
#define ISTRIP  0000200
#define IUCLC   0000400
#define IXANY   0001000
#define IXOFF   0002000
#define IXON    0004000
#define PARMRK  0010000

/* flags for output modes */
#define OPOST   0000001
#define OLCUC   0000002
#define ONLCR   0000004
#define OCRNL   0000010
#define ONOCR   0000020
#define ONLRET  0000040
#define OFILL   0000100
#define OFDEL   0000200
#define NLDLY   0000400
#define   NL0   0000000
#define   NL1   0000400
#define CRDLY   0003000
#define   CR0   0000000
#define   CR1   0001000
#define   CR2   0002000
#define   CR3   0003000
#define TABDLY  0014000
#define   TAB0  0000000
#define   TAB1  0004000
#define   TAB2  0010000
#define   TAB3  0014000
#define BSDLY   0020000
#define   BS0   0000000
#define   BS1   0020000
#define FFDLY   0100000
#define   FF0   0000000
#define   FF1   0100000
#define VTDLY   0040000
#define   VT0   0000000
#define   VT1   0040000

/* baud rates */
#define B0      0000000
#define B50     0000001
#define B75     0000002
#define B110    0000003
#define B134    0000004
#define B150    0000005
#define B200    0000006
#define B300    0000007
#define B600    0000010
#define B1200   0000011
#define B1800   0000012
#define B2400   0000013
#define B4800   0000014
#define B9600   0000015
#define B19200  0000016
#define B38400  0000017

/* control modes */
#define CSIZE   0000060
#define   CS5   0000000
#define   CS6   0000020
#define   CS7   0000040
#define   CS8   0000060
#define CSTOPB  0000100
#define CREAD   0000200
#define PARENB  0000400
#define PARODD  0001000
#define HUPCL   0002000
#define CLOCAL  0004000

/* local modes */
#define ISIG    0000001
#define ICANON  0000002
#define XCASE   0000004
#define ECHO    0000010
#define ECHOE   0000020
#define ECHOK   0000040
#define ECHONL  0000100
#define NOFLSH  0000200
#define TOSTOP  0000400
#define IEXTEN  0001000

/* attributes */
#define TCSANOW   0x0001
#define TCSADRAIN 0x0002
#define TCSAFLUSH 0x0004

#define TCIFLUSH  0x0001
#define TCIOFLUSH 0x0003
#define TCOFLUSH  0x0002

#define TCIOFF    0x0001
#define TCION     0x0002
#define TCOOFF    0x0004
#define TCOON     0x0008

#define NCCS 32

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t     c_cc[NCCS];
};

/* ioctl commands */

#define TCGETS   0x4000 /* Get termios struct */
#define TCSETS   0x4001 /* Set termios struct */
#define TCSETSW  0x4002 /* Set, but let drain first */
#define TCSETSF  0x4003 /* Set, but let flush first */

#define TCGETA   TCGETS
#define TCSETA   TCSETS
#define TCGETAW  TCGETSW
#define TCGETAF  TCGETSF

#define TCSBRK   0x4004
#define TCXONC   0x4005
#define TCFLSH   0x4006

#define TIOCEXCL     0x4007
#define TIOCNXCL     0x4008
#define TIOCSCTTY    0x4009
#define TIOCGPGRP    0x400A
#define TIOCSPGRP    0x400B
#define TIOCOUTQ     0x400C
#define TIOCSTI      0x400D
#define TIOCGWINSZ   0x400E
#define TIOCSWINSZ   0x400F
#define TIOCMGET     0x4010
#define TIOCMBIS     0x4011
#define TIOCMBIC     0x4012
#define TIOCMSET     0x4013
#define TIOCGSOFTCAR 0x4014
#define TIOCSSOFTCAR 0x4015

/* termios functions */
#ifndef _KERNEL_
speed_t cfgetispeed(const struct termios *);
speed_t cfgetospeed(const struct termios *);
int     cfsetispeed(struct termios *, speed_t);
int     cfsetospeed(struct termios *, speed_t);
int     tcdrain(int);
int     tcflow(int, int);
int     tcflush(int, int);
int     tcgetattr(int, struct termios *);
pid_t   tcgetsid(int);
int     tcsendbreak(int, int);
int     tcsetattr(int, int, struct termios *);
int     ioctl(int, int, void*);
#endif /* ndef _KERNEL_ */

#endif /* ndef _TERMIOS_H */
