
/* Signal names (from the Unix specification on signals) */
#define SIGHUP      1  /* Hangup */
#define SIGINT      2  /* Interupt */
#define SIGQUIT     3  /* Quit */
#define SIGILL      4  /* Illegal instruction */
#define SIGTRAP     5  /* A breakpoint or trace instruction has been reached */
#define SIGABRT     6  /* Another process has requested that you abort */
#define SIGEMT      7  /* Emulation trap XXX */
#define SIGFPE      8  /* Floating-point arithmetic exception */
#define SIGKILL     9  /* You have been stabbed repeated with a large knife */
#define SIGBUS      10 /* Bus error (device error) */
#define SIGSEGV     11 /* Segmentation fault */
#define SIGSYS      12 /* Bad system call */
#define SIGPIPE     13 /* Attempted to read or write from a broken pipe */
#define SIGALRM     14 /* This is your wakeup call. */
#define SIGTERM     15 /* You have been Schwarzenegger'd */
#define SIGUSR1     16 /* User Defined Signal #1 */
#define SIGUSR2     17 /* User Defined Signal #2 */
#define SIGCHLD     18 /* Child status report */
#define SIGPWR      19 /* We need moar powah! */
#define SIGWINCH    20 /* Your containing terminal has changed size */
#define SIGURG      21 /* An URGENT! event (On a socket) */
#define SIGPOLL     22 /* XXX OBSOLETE; socket i/o possible */
#define SIGSTOP     23 /* Stopped (signal) */
#define SIGTSTP     24 /* ^Z (suspend) */
#define SIGCONT     25 /* Unsuspended (please, continue) */
#define SIGTTIN     26 /* TTY input has stopped */
#define SIGTTOUT    27 /* TTY output has stopped */
#define SIGVTALRM   28 /* Virtual timer has expired */
#define SIGPROF     29 /* Profiling timer expired */
#define SIGXCPU     30 /* CPU time limit exceeded */
#define SIGXFSZ     31 /* File size limit exceeded */
#define SIGWAITING  32 /* Herp */
#define SIGDIAF     33 /* Die in a fire */
#define SIGHATE     34 /* The sending process does not like you */
#define SIGWINEVENT 35 /* Window server event */
#define SIGCAT      36 /* Everybody loves cats */

#define SIGTTOU     37

#define NUMSIGNALS  38
#define NSIG        NUMSIGNALS
