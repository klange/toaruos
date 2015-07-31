#include <module.h>
#include <logging.h>
#include <printf.h>
#include <mod/shell.h>
#include <mod/net.h>
#include <ipv4.h>

static spin_lock_t irc_tty_lock = { 0 };

static char irc_input[400] = {'\0'};
static char irc_prompt[100] = {'\0'};
static char irc_nick[32] = {'\0'};
static char irc_payload[512];
static struct socket * irc_socket = NULL;

static char read_a_byte(struct socket * stream) {
	static char * foo = NULL;
	static char * read_ptr = NULL;
	static int have_bytes = 0;
	if (!foo) foo = malloc(4096);
	while (!have_bytes) {
		memset(foo, 0x00, 4096);
		have_bytes = net_recv(stream, (uint8_t *)foo, 4096);
		debug_print(WARNING, "Received %d bytes...", have_bytes);
		read_ptr = foo;
	}

	char ret = *read_ptr;

	have_bytes -= 1;
	read_ptr++;

	return ret;
}


static char * fgets(char * buf, int size, struct socket * stream) {
	char * x = buf;
	int collected = 0;

	while (collected < size) {

		*x = read_a_byte(stream);

		collected++;

		if (*x == '\n') break;

		x++;
	}

	x++;
	*x = '\0';
	return buf;
}

static void irc_send(char * payload) {
	/* Uh, stuff */
	net_send(irc_socket, (uint8_t *)payload, strlen(payload), 0);
}

static int tty_readline(fs_node_t * dev, char * linebuf, int max) {
	int read = 0;
	tty_set_unbuffered(dev);
	while (read < max) {
		uint8_t buf[1];
		int r = read_fs(dev, 0, 1, (unsigned char *)buf);
		if (!r) {
			debug_print(WARNING, "Read nothing?");
			continue;
		}
		spin_lock(irc_tty_lock);
		linebuf[read] = buf[0];
		if (buf[0] == '\n') {
			linebuf[read] = 0;
			spin_unlock(irc_tty_lock);
			break;
		} else if (buf[0] == 0x08) {
			if (read > 0) {
				fprintf(dev, "\010 \010");
				read--;
				linebuf[read] = 0;
			}
		} else if (buf[0] < ' ') {
			switch (buf[0]) {
				case 0x0C: /* ^L */
					/* Should reset display here */
					spin_unlock(irc_tty_lock);
					break;
				default:
					/* do nothing */
					spin_unlock(irc_tty_lock);
					break;
			}
		} else {
			fprintf(dev, "%c", buf[0]);
			read += r;
		}
		spin_unlock(irc_tty_lock);
	}
	tty_set_buffered(dev);
	return read;
}

static void handle_irc_packet(fs_node_t * tty, size_t size, uint8_t * packet) {
	char * c = (char *)packet;

	while ((uintptr_t)c < (uintptr_t)packet + size) {
		char * e = strstr(c, "\r\n");

		if ((uintptr_t)e > (uintptr_t)packet + size) {
			break;
		}
		spin_lock(irc_tty_lock);

		if (!e) {
			/* XXX */
			c[size-1] = '\0';
			fprintf(tty, "\r\033[36m%s\033[0m\033[K\n", c);
			goto prompt_;
		}

		e[0] = '\0';

		if (startswith(c, "PING")) {
			char tmp[100];
			char * t = strstr(c, ":");
			sprintf(tmp, "PONG %s\r\n", t);
			irc_send(tmp);
			goto prompt_;
		}

		char * user;
		char * command;
		char * channel;
		char * message;

		user = c;

		command = strstr(user, " ");
		if (!command) {
			fprintf(tty, "\r\033[36m%s\033[0m\033[K\n", user);
			goto prompt_;
		}
		command[0] = '\0';
		command++;

		channel = strstr(command, " ");
		if (!channel) {
			fprintf(tty, "\r\033[36m%s %s\033[0m\033[K\n", user, command);
			goto prompt_;
		}
		channel[0] = '\0';
		channel++;

		if (!strcmp(command, "PRIVMSG")) {
			message = strstr(channel, " ");
			if (!message) {
				fprintf(tty, "\r\033[36m%s %s %s\033[0m\033[K\n", user, command, channel);
				goto prompt_;
			}
			message[0] = '\0';
			message++;
			if (message[0] == ':') { message++; }
			if (user[0] == ':') { user++; }
			char * t = strstr(user, "!");
			if (t) { t[0] = '\0'; }
			t = strstr(user, "@");
			if (t) { t[0] = '\0'; }
			uint16_t hr, min, sec;
			get_time(&hr, &min, &sec);

			if (startswith(message, "\001ACTION ")) {
				message = message + 8;
				char * x = strstr(message, "\001");
				if (x) *x = '\0';
				fprintf(tty, "\r%2d:%2d:%2d * \033[32m%s\033[0m:\033[34m%s\033[0m %s\033[K\n", hr, min, sec, user, channel, message);
			} else {
				fprintf(tty, "\r%2d:%2d:%2d \033[90m<\033[32m%s\033[0m:\033[34m%s\033[90m>\033[0m %s\033[K\n", hr, min, sec, user, channel, message);
			}
		} else {
			fprintf(tty, "\r\033[36m%s %s %s\033[0m\033[K\n", user, command, channel);
		}

prompt_:
		/* Redraw prompt */
		fprintf(tty, "%s", irc_prompt);
		fprintf(tty, "%s", irc_input);

		spin_unlock(irc_tty_lock);

		if (!e) break;

		c = e + 2;
	}
}



static void ircd(void * data, char * name) {
	fs_node_t * tty = data;
	char * buf = malloc(4096);

	while (1) {
		char * result = fgets(buf, 4095, irc_socket);
		if (!result) continue;
		size_t len = strlen(buf);
		if (!len) continue;

		handle_irc_packet(tty, len, (unsigned char *)buf);
	}
}

DEFINE_SHELL_FUNCTION(irc_init, "irc connector") {
	/* TODO set up IRC socket */
	irc_socket = net_open(SOCK_STREAM);
	net_connect(irc_socket, ip_aton("10.255.50.206"), 1025);

	fprintf(tty, "[irc] Socket is at 0x%x\n", irc_socket);

	/* TODO set up IRC daemon */
	create_kernel_tasklet(ircd, "[ircd]", tty);

	return 0;
}

DEFINE_SHELL_FUNCTION(irc_nick, "irc nick") {
	if (argc < 2) {
		fprintf(tty, "Specify a username\n");
		return 1;
	}

	fprintf(tty, "[irc] Sending name...\n");
	memcpy(irc_nick, argv[1], strlen(argv[1])+1);

	sprintf(irc_payload, "NICK %s\r\nUSER %s * 0 :%s\r\n"
			"PASS %s:%s\r\n", irc_nick, irc_nick, irc_nick, irc_nick, "Mqlsfanpra");
	irc_send(irc_payload);
	return 0;
}


DEFINE_SHELL_FUNCTION(irc_join, "irc channel tool") {

	if (argc < 2) {
		fprintf(tty, "Specify a channel.\n");
		return 1;
	}

	char * channel = argv[1];

	sprintf(irc_payload, "JOIN %s\r\n", channel);
	irc_send(irc_payload);

	sprintf(irc_prompt, "\r[%s] ", channel);

	while (1) {
		fprintf(tty, irc_prompt);
		int c = tty_readline(tty, irc_input, 400);

		spin_lock(irc_tty_lock);

		irc_input[c] = '\0';

		if (startswith(irc_input, "/part")) {
			fprintf(tty, "\n");
			sprintf(irc_payload, "PART %s\r\n", channel);
			irc_send(irc_payload);
			spin_unlock(irc_tty_lock);
			break;
		}

		if (startswith(irc_input, "/me ")) {
			char * m = strstr(irc_input, " ");
			m++;
			uint16_t hr, min, sec;
			get_time(&hr, &min, &sec);
			fprintf(tty, "\r%2d:%2d:%2d * \033[35m%s\033[0m:\033[34m%s\033[0m %s\n\033[K", hr, min, sec, irc_nick, channel, m);
			sprintf(irc_payload, "PRIVMSG %s :\1ACTION %s\1\r\n", channel, m);
			irc_send(irc_payload);
		} else {
			uint16_t hr, min, sec;
			get_time(&hr, &min, &sec);
			fprintf(tty, "\r%2d:%2d:%2d \033[90m<\033[35m%s\033[0m:\033[34m%s\033[90m>\033[0m %s\n\033[K", hr, min, sec, irc_nick, channel, irc_input);
			sprintf(irc_payload, "PRIVMSG %s :%s\r\n", channel, irc_input);
			irc_send(irc_payload);
		}

		memset(irc_input, 0x00, sizeof(irc_input));
		spin_unlock(irc_tty_lock);
	}
	memset(irc_prompt, 0x00, sizeof(irc_prompt));
	memset(irc_input, 0x00, sizeof(irc_input));

	return 0;
}

DEFINE_SHELL_FUNCTION(http, "lol butts") {
	struct socket * s = net_open(SOCK_STREAM);
	net_connect(s, ip_aton("104.16.56.23"), 80);

	char * buf = "GET /version HTTP/1.0\r\n"
	             "User-Agent: curl/7.35.0\r\n"
	             "Host: www.yelp.com\r\n"
	             "Accept: */*\r\n"
	             "\r\n";

	net_send(s, buf, strlen(buf), 0);

	char * foo = malloc(4096);
	memset(foo, 0, 4096);

	size_t size = 0;
	while (!size) {
		size = net_recv(s, foo, 4096);
		fprintf(tty, "Received response from server of size %d: %s\n", size, foo);
	}
	free(foo);

	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(irc_init);
	BIND_SHELL_FUNCTION(irc_nick);
	BIND_SHELL_FUNCTION(irc_join);
	BIND_SHELL_FUNCTION(http);

	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(irc, init, fini);
MODULE_DEPENDS(debugshell);
MODULE_DEPENDS(net);
