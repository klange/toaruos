#ifndef TELNET_H
#define TELNET_H

/* Telnet Defines */
#define IAC   255
#define DONT  254
#define DO    253
#define WONT  252
#define WILL  251

#define SE  240  // Subnegotiation End
#define NOP 241  // No Operation
#define DM  242  // Data Mark
#define BRK 243  // Break
#define IP  244  // Interrupt process
#define AO  245  // Abort output
#define AYT 246  // Are You There
#define EC  247  // Erase Character
#define EL  248  // Erase Line
#define GA  249  // Go Ahead
#define SB  250  // Subnegotiation Begin

#define BINARY 0 // 8-bit data path
#define ECHO 1 // echo
#define RCP 2 // prepare to reconnect
#define SGA 3 // suppress go ahead
#define NAMS 4 // approximate message size
#define STATUS 5 // give status
#define TM 6 // timing mark
#define RCTE 7 // remote controlled transmission and echo
#define NAOL 8 // negotiate about output line width
#define NAOP 9 // negotiate about output page size
#define NAOCRD 10 // negotiate about CR disposition
#define NAOHTS 11 // negotiate about horizontal tabstops
#define NAOHTD 12 // negotiate about horizontal tab disposition
#define NAOFFD 13 // negotiate about formfeed disposition
#define NAOVTS 14 // negotiate about vertical tab stops
#define NAOVTD 15 // negotiate about vertical tab disposition
#define NAOLFD 16 // negotiate about output LF disposition
#define XASCII 17 // extended ascii character set
#define LOGOUT 18 // force logout
#define BM 19 // byte macro
#define DET 20 // data entry terminal
#define SUPDUP 21 // supdup protocol
#define SUPDUPOUTPUT 22 // supdup output
#define SNDLOC 23 // send location
#define TTYPE 24 // terminal type
#define EOR 25 // end or record
#define TUID 26 // TACACS user identification
#define OUTMRK 27 // output marking
#define TTYLOC 28 // terminal location number
#define VT3270REGIME 29 // 3270 regime
#define X3PAD 30 // X.3 PAD
#define NAWS 31 // window size
#define TSPEED 32 // terminal speed
#define LFLOW 33 // remote flow control
#define LINEMODE 34 // Linemode option
#define XDISPLOC 35 // X Display Location
#define OLD_ENVIRON 36 // Old - Environment variables
#define AUTHENTICATION 37 // Authenticate
#define ENCRYPT 38 // Encryption option
#define NEW_ENVIRON 39 // New - Environment variables
#define TN3270E 40 // TN3270E
#define XAUTH 41 // XAUTH
#define CHARSET 42 // CHARSET
#define RSP 43 // Telnet Remote Serial Port
#define COM_PORT_OPTION 44 // Com Port Control Option
#define SUPPRESS_LOCAL_ECHO 45 // Telnet Suppress Local Echo
#define TLS 46 // Telnet Start TLS
#define KERMIT 47 // KERMIT
#define SEND_URL 48 // SEND-URL
#define FORWARD_X 49 // FORWARD_X
#define PRAGMA_LOGON 138 // TELOPT PRAGMA LOGON
#define SSPI_LOGON 139 // TELOPT SSPI LOGON
#define PRAGMA_HEARTBEAT 140 // TELOPT PRAGMA HEARTBEAT
#define EXOPL 255 // Extended-Options-List
#define NOOPT 0

#define IS 0
#define SEND 1

#endif
