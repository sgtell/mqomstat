/*
 * General-purpose serial I/O routines.  Oriented towards talking to
 * modems and embedded devices.   Not perfect, but still quite useful.
 * by Steve Tell

 * Copyright (C) 1997 Stephen G. Tell, tell@cs.unc.edu

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 * $Log: tty.c,v $
 * Revision 1.1  2002/02/25 04:53:36  sgt
 * initial checkin of autoconfiscated and renamed system
 *
 * Revision 1.1  1997/05/22 04:13:57  tell
 * Initial revision
 *
 */

#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "tty.h"
#include "asciiutils.h"

extern int g_verbose;

typedef struct _tty_speed
{
	int code;
	int speed;
	
} tty_speed;

tty_speed tty_speeds[] =
{
	{B50, 50},
	{B75, 75},
	{B110, 110},
	{B134, 134},
	{B150, 150},
	{B200, 200},
	{B300, 300},
	{B600, 600},
	{B1200, 1200},
	{B1800, 1800},
	{B2400, 2400},
	{B4800, 4800},
	{B9600, 9600},
	{B19200, 19200},
	{B38400, 38400},
#ifdef B57600
	{B57600, 57600},
#endif
#ifdef B115200
	{B115200, 115200},
#endif
#ifdef B230400
	{B230400, 230400},
#endif
	{-1, -1}
};


typedef struct _tty_size
{
	int code;
	int size;
	
} tty_size;

tty_size tty_sizes[] =
{
	{CS5, 5},
	{CS6, 6},
	{CS7, 7},
	{CS8, 8},
	{-1, -1}
};

/*
 * Open serial terminal device.  
 * Will not block opening the device, but subsequently arranges for reads
 * to block.
 */
int tty_open(char *device)
{
	int fd, flags;
	
	/* set O_NDELAY so open() returns without waiting for carrier */
	if((fd = open(device, O_RDWR | O_NDELAY | O_NOCTTY, 0)) < 0)
	{
		perror(device);
		return -1;
	}								   
	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) {
		fprintf(stderr, "fcntl(%s, F_GETFL): %s", device, strerror(errno));
		close(fd);
		return -1;
	}
	if(fcntl(fd, F_SETFL, flags & ~O_NDELAY) < 0) {
		fprintf(stderr, "fcntl(%s, F_SETFL): %s", device, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

/* we define a corresponding close routine just for symetry's sake. */
void tty_close(int fd)
{
	close(fd);
}

int tty_isspeed(int speed)
{
	tty_speed *s;

	for (s = &tty_speeds[0]; s->code != -1; s++) {
		if (s->speed == speed) {
			return s->code;
		}
	}
	return -1;
}

/*
 * set basic serial parameters:
 *	bit rate, number of data bits, number of stop bits, parity.
 *	also sets to raw mode.
 */
int tty_set(int fd, int speed, int data, int stop, int parity)
{
	int scode;
	tty_speed *s;
	tty_size *d;
	struct termios t;

	if (tcgetattr(fd, &t) < 0) {
		fprintf(stderr, "tcgetattr(): %s\n", strerror(errno));
		return -1;
	}
	cfmakeraw(&t);
	t.c_cc[VMIN]  = 1;
	t.c_cc[VTIME] = 0;

	scode = tty_isspeed(speed);
	if (scode == -1) {
		fprintf(stderr, "speed %d not supported\n", speed);
		return -1;
	}
	cfsetispeed(&t, scode);
	cfsetospeed(&t, scode); 

	for (d = tty_sizes; d->size != data; d++) {
		if (d->code == -1) {
			fprintf(stderr, "%d data bits not supported\n", data);
			return -1;
		}
	}
	t.c_cflag |= d->code;

	switch (stop) {
	case 1:
		break;
	case 2:
		t.c_cflag |= CSTOPB;
		break;
	default:
		fprintf(stderr, "%d stop bits not supported\n", stop);
		return -1;
	}

	switch (parity)	{
	case NO_PARITY:
		break;
	case ODD_PARITY:
		t.c_cflag |= PARENB;
	case EVEN_PARITY:
		t.c_cflag |= PARODD;
		break;
	default:
		fprintf(stderr, "parity must be odd, even or none\n");
		return -1;
	}

	if (tcsetattr(fd, TCSANOW, &t) < 0) {
		fprintf(stderr, "tcsetattr(): %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/* put tty in "raw" mode
 */
int tty_raw(int fd)
{
	struct termios t;

	if (tcgetattr(fd, &t) < 0) {
		fprintf(stderr, "tcgetattr(): %s\n", strerror(errno));
		return -1;
	}

	cfmakeraw(&t);
	t.c_cflag |= CLOCAL;
	t.c_cflag |= HUPCL;
	t.c_cc[VMIN]  = 1;
	t.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSAFLUSH, &t) < 0) {
		fprintf(stderr, "tcsetattr(): %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/* put tty in "cooked" mode for normal line-mode interaction.
 *  This sets all parameters from scratch, and is meant for applications
 *  where there is no getty(8) on the line.
 */

int tty_cooked(int fd)
{
	struct termios t;

	if (tcgetattr(fd, &t) < 0) {
		fprintf(stderr, "tcgetattr(): %s\n", strerror(errno));
		return -1;
	}

	t.c_cflag &= ~CLOCAL;
	t.c_cflag |= HUPCL;
	t.c_cflag |= CRTSCTS;
	t.c_iflag |= IXON | IXOFF | ICRNL;
	t.c_oflag |= OPOST | ONLCR;
	t.c_lflag |= ICANON | ISIG | ECHO | ECHOE | ECHOK;
	if (t.c_cflag & PARENB)
		t.c_lflag |= INPCK | ISTRIP;
	t.c_cc[VINTR]    = CTL('C');
	t.c_cc[VQUIT]    = CTL('\\');
	t.c_cc[VERASE]   = CTL('H');
	t.c_cc[VKILL]    = CTL('U');
	t.c_cc[VEOF]     = CTL('D');
	t.c_cc[VEOL]     = 0;
	t.c_cc[VEOL2]    = 0;
#ifdef VTSWTCH
	t.c_cc[VSWTCH]   = 0;
#endif
	t.c_cc[VSTART]   = CTL('Q');
	t.c_cc[VSTOP]    = CTL('S');
	t.c_cc[VSUSP]    = CTL('Z');
	t.c_cc[VREPRINT] = CTL('R');
	t.c_cc[VDISCARD] = CTL('O');
	t.c_cc[VWERASE]  = CTL('W');
	t.c_cc[VLNEXT]   = CTL('V');

	if (tcsetattr(fd, TCSAFLUSH, &t) < 0) {
		fprintf(stderr, "tcsetattr(): %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/* hang up a modem by turning off control lines for a short while */
int tty_hangup(int fd)
{		
	int mask = TIOCM_DTR | TIOCM_RTS;

	if (ioctl(fd, TIOCMBIC, &mask) < 0) {
		fprintf(stderr, "can not clear modem control lines");
		return -1;
	}
	sleep(WAIT_HANGUP);
	if (ioctl(fd, TIOCMBIS, &mask) < 0) {
		fprintf(stderr, "can not set modem control lines");
		return -1;
	}

	return 0;
}

int tty_discard(int fd)
{
	if (tcflush(fd, TCIOFLUSH) < 0)	{
		perror("tcflush(TCIOFLUSH)");
		return -1;
	}
	return 0;
}
 
int tty_flush(int fd)
{
	/* fflush(stdout); */
	tcdrain(fd);
	return 0;
}

#if 0
int tty_ctty(int fd)
{
	if( (getuid()==0) && (ioctl(fd, TIOCSCTTY, 1) <= 0)) {
		/*
		fprintf(stderr, "ioctl(TIOCSCTTY)");
		return -1;
		*
		* Usually seems to return EBADF but set ctty anyway, so
		* don't complain.
		*/
		return 0;
	} else {
		return 0;
	}
}
#endif

int
tty_softcar(int fd, int soft)
{
	if(ioctl(fd, TIOCSSOFTCAR, &soft) == -1) {
		fprintf(stderr, "can not set tty software carrier to %d", soft);
		return -1;
	}
	return 0;
}

int
tty_puts(int fd, char *s)
{
	int len, n;
	len = strlen(s);
	n = write(fd, s, len);
	if(n == len)
		return 0;
	else
		return -1;
}

/*
 * wait for character.
 * uses alarm(3).  Caller must set up signal handler if desired.
 */
int tty_waitforchar(int fd, char c, int timeout, int echo)
{
	char buf[256];
	int n;
	if(timeout)
		alarm(timeout);
	do {
		n = read(fd, buf, 1);
		if(n == -1) {
			perror("waitforchar: read");
			return -1;
		}
		if(echo) {
			xprint(stdout, buf, n);
			fflush(stdout);
		}
	} while(memchr(buf, c, n) == NULL);
	if(timeout)
		alarm(0);
	return 0;
}

/*
 * Read a "line."  Can use any character as the end-of-line indicator.
 * Again, if a timeout is desired, the caller can catch SIGALARM.
 * If the line is longer than the user's buffer length, excess is discarded.
 */
int tty_getline(int fd, char *buf, int len, char term, int timeout, int echo)
{
	char rbuf[4];
	int n, nr;
	nr = 0;
	alarm(timeout);
	do {
		n = read(fd, rbuf, 1);
		if(n == -1) {
			perror("getline: read");
			return -1;
		}
		if(echo) {
			xprint(stdout, rbuf, n);
			fflush(stdout);
		}
		if(nr < len) {
			buf[nr++] = rbuf[0];
		}
	} while(rbuf[0] != term);
	alarm(0);
	return 0;
}

/* transmit on/off control for half-duplex RS485 and such */

#ifdef TTY_XMIT_IS_DTR
#define TTY_XBIT TIOCM_DTR
#else
#define TTY_XBIT TIOCM_RTS
#endif

void
tty_xmit_on(int fd)
{
	int xbit = TTY_XBIT;
	ioctl(fd, TIOCMBIS, &xbit);
}

void
tty_xmit_off(int fd)
{
	int xbit = TTY_XBIT;
	ioctl(fd, TIOCMBIC, &xbit);
}
