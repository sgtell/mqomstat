/*
 * tty.h - declarations for serial I/O routines
 * 
 * Copyright (C) 1998 Stephen G. Tell, tell@cs.unc.edu

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
*/

#ifndef _TTY_H
#define _TTY_H

#define NO_PARITY		0
#define EVEN_PARITY		1
#define ODD_PARITY		2

#define CTL(x)          (x ^ 0100)

#define WAIT_HANGUP	5

int tty_isspeed(int speed);
int tty_open(char *tty);
void tty_close(int fd);
int tty_set(int fd, int speed, int data, int stop, int parity);
int tty_raw(int fd);
int tty_cooked(int fd);
int tty_hangup(int fd);
int tty_discard(int fd);
int tty_blocking(int fd);
int tty_flush(int fd);
int tty_ctty(int fd);
void tty_own(char *tty);
int tty_softcar(int fd, int soft);

#endif
