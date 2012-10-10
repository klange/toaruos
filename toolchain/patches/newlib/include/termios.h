#ifndef TERMIOS_H
#define TERMIOS_H

/* types defined in POSIX */
typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

/* number of entries in c_cc */
#define NCCS 32

typedef struct termios {
	tcflag_t c_iflag;    /* input mode flags   */
	tcflag_t c_oflag;    /* output mode flags  */
	tcflag_t c_cflag;    /* control modes      */
	tcflag_t c_lflag;    /* local modes        */
	cc_t     c_cc[NCCS]; /* control characters */
} termios;

/* c_cc offsets */
#define VEOF    1
#define VEOL    2
#define VERASE  3
#define VINTR   4
#define VKILL   5
#define VMIN    6
#define VQUIT   7
#define VSTART  8
#define VSTOP   9
#define VSUSP   10
#define VTIME   11

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

#endif
