/*
 * vt100.c  ANSI/VT102 emulator code
 *
 * Copyright (C) 1991-1995 Miquel van Smoorenburg
 * Copyright (C) 2015-2017 Parallels IP Holdings GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

#include <rfb/keysym.h>
#include "vt100.h"

/*
 * The global variable esc_s holds the escape sequence status:
 * 0 - normal
 * 1 - ESC
 * 2 - ESC [
 * 3 - ESC [ ?
 * 4 - ESC (
 * 5 - ESC )
 * 6 - ESC #
 * 7 - ESC P
 */
static int esc_s = 0;

#define ESC 27
#define ESCPARMS_SIZE 16

static unsigned char vt_fg;		/* Standard foreground color. */
static unsigned char vt_bg;		/* Standard background color. */

static unsigned short escparms[ESCPARMS_SIZE];		/* Cumulated escape sequence. */
static int ptr;                 /* Index into escparms array. */

static short newy1 = 0;		/* Current size of scrolling region. */
static short newy2 = 23;

static void state1(vncConsole *console, unsigned char c);
static void state2(vncConsole *console, unsigned char c);
static void state3(vncConsole *console, unsigned char c);
static void state6(unsigned char c);


void vt_init(vncConsole *console)
{
	memset(escparms, 0, sizeof(escparms));
	memset(console->screenBuffer, ' ', console->width * console->height);
	console->x=0;
	console->y=0;
	console->cursorActive=TRUE;
	vt_fg = WHITE;
	vt_bg = BLACK;
}

void vt_out(vncConsole *console, unsigned char c)
{
	static unsigned char last_ch;
	int go_on = 0;

	if (c == 0)
		return;
	last_ch = c;(void)last_ch;

	if (esc_s) {
		fprintf(stdout, "_%c ", c);
	} else {
		if (isprint(c)) {
			fprintf(stdout, "%c", c);
		} else {
			fprintf(stdout, "\n0x%x ", c);
		}
	}
	fflush(stdout);

	/* Process <31 chars first, even in an escape sequence. */
	switch (c) {
	case '\r': /* Carriage return */
		vcPutCharColour(console, c, vt_fg, vt_bg);
		break;
	case '\t': /* Non - destructive TAB */
		vcPutCharColour(console, c, vt_fg, vt_bg);
		break;
	case 013: /* Old Minix: CTRL-K = up */
		fprintf(stdout, "Old Minix: CTRL-K = up\n");
		break;
	case '\f': /* Form feed: clear screen. */
		fprintf(stdout, "Form feed: clear screen\n");
		break;
	case 14:
	case 15:  /* Change character set. Not supported. see original vt100.c */
		break;
	case 24:
	case 26:  /* Cancel escape sequence. */
		esc_s = 0;
		break;
	case ESC: /* Begin escape sequence */
		esc_s = 1;
		break;
	case 128+ESC: /* Begin ESC [ sequence. */
		esc_s = 2;
		break;
	case '\b': /* Backspace */

		vcHideCursor(console);
		console->x--;
//		vcPutCharColour(console, ' ', vt_fg, vt_bg);
//		console->x--;
		vcDrawCursor(console);
		break;

	case '\n':
		vcPutCharColour(console, c, vt_fg, vt_bg);
		break;
	case 7: /* Bell */
		rfbSendBell( console->screen );
		break;
	default:
		go_on = 1;
		break;
	}
	if (!go_on)
		return;

	/* Now see which state we are in. */
	switch (esc_s) {
	case 0: /* Normal character */
		vcPutCharColour(console, c, vt_fg, vt_bg);
		break;
	case 1: /* ESC seen */
		state1(console, c);
		break;
	case 2: /* ESC [ ... seen */
		state2(console, c);
		break;
	case 3:
		state3(console, c);
		break;
	case 4:
		fprintf(stdout, "Switch Character Sets 4\n");
		break;
	case 5:
		fprintf(stdout, "Switch Character Sets 5\n");
		break;
	case 6:
		state6(c);
		break;
	case 7:
		fprintf(stdout, "Device dependant control strings\n");
		break;
	}
}

/*
 * Escape code handling.
 */

/*
 * ESC was seen the last time. Process the next character.
 */
static void state1(vncConsole *console, unsigned char c)
{
	switch(c) {
	case '[': /* ESC [ */
		esc_s = 2;
		return;
	case '(': /* ESC ( */
		esc_s = 4;
		return;
	case ')': /* ESC ) */
		esc_s = 5;
		return;
	case '#': /* ESC # */
		esc_s = 6;
		return;
	case 'P': /* ESC P (DCS, Device Control String) */
		esc_s = 7;
		return;
	case 'D': /* Cursor down */
		fprintf(stdout, "Cursor down\n");
		break;
	case 'M': /* Cursor up */
		// locate or scroll
		fprintf(stdout, "Cursor up\n");
		break;
	case 'E': /* CR + NL */
		vcPutChar(console, '\r');
		vcPutChar(console, '\n');
		break;
	case '7': /* Save attributes and cursor position */
	case 's':
		fprintf(stdout, "Save attributes and cursor position\n");
		break;
	case '8': /* Restore them */
	case 'u':
		fprintf(stdout, "Restore attributes and cursor position\n");
		break;
	case '=': /* Keypad into applications mode */
		fprintf(stdout, "Keypad into applications mode\n");
		break;
	case '>': /* Keypad into numeric mode */
		fprintf(stdout, "Keypad into numeric mode\n");
		break;
	case 'Z': /* Report terminal type */
		fprintf(stdout, "Report terminal type\n");
		break;
	case 'c': /* Reset to initial state */
		fprintf(stdout, "Reset to initial state\n");
		vcHideCursor(console);
		vcReset(console);
		break;
	case 'H': /* Set tab in current position */
		fprintf(stdout, "Set tab in current position\n");
		break;
	case 'N': /* G2 character set for next character only*/
	case 'O': /* G3 "				"    */
	case '<': /* Exit vt52 mode */
	default:
		/* ALL IGNORED */
		break;
	}
	esc_s = 0;
}

/* ESC [ ... [hl] seen. */
static void ansi_mode(int on_off)
{
	int i;

	for (i = 0; i <= ptr; i++) {
		switch (escparms[i]) {
		case 4: /* Insert mode  */
			fprintf(stdout, "Insert mode %d\n", on_off);
			break;
		case 20: /* Return key mode */
			fprintf(stdout, "Return key mode %d\n", on_off);
			break;
		}
	}
}

/*
 * ESC [ ... was seen the last time. Process next character.
 */
static void state2(vncConsole *console, unsigned char c)
{
	unsigned short x, y, f;
	unsigned char attr;

	/* See if a number follows */
	if (c >= '0' && c <= '9') {
		escparms[ptr] = 10*escparms[ptr] + c - '0';
		return;
	}
	/* Separation between numbers ? */
	if (c == ';') {
		if (ptr < ESCPARMS_SIZE - 1)
			ptr++;
		return;
	}
	/* ESC [ ? sequence */
	if (escparms[0] == 0 && ptr == 0 && c == '?')
	{
		esc_s = 3;
		return;
	}

/* Process functions with zero, one, two or more arguments */
	switch (c) {
	case 'A':
	case 'B':
	case 'C':
	case 'D':
		fprintf(stdout, "Cursor motion [%c, %d]\n", c, escparms[0]);
		if ((f = escparms[0]) == 0)
			f = 1;
		x = console->x;
		y = console->y;
		x += f * ((c == 'C') - (c == 'D'));
		if (x < 0)
			x = 0;
		if (x >= console->width)
			x = console->width - 1;
		if (c == 'B') { /* Down. */
			y += f;
			if (y >= console->height)
				y = console->height - 1;
			if (y >= newy2 + 1)
				y = newy2;
		} else if (c == 'A') { /* Up. */
			y -= f;
			if (y < 0)
				y = 0;
			if (y <= newy1 - 1)
				y = newy1;
		}
		vcHideCursor(console);
		console->x = x;
		console->y = y;
		fprintf(stdout, "x=%d y=%d\n", console->x, console->y);
		break;
	case 'H':
		fprintf(stdout, "Set cursor position (%d, %d)\n", escparms[0], escparms[1]);
		if ((y = escparms[0]) == 0)
			y = 1;
		if ((x = escparms[1]) == 0)
			x = 1;
//		if (vt_om)
//			y += newy1;
		if (x >= console->width)
			x = console->width;
		if (y >= console->height)
			y = console->height;
		vcHideCursor(console);
		console->x = x - 1;
		console->y = y - 1;
		break;
	case 'X': /* Character erasing (ECH) */
		fprintf(stdout, "Character erasing (ECH)\n");
		break;
	case 'K': /* Line erasing */
		fprintf(stdout, "Line erasing (%d)\n", escparms[0]);
		switch (escparms[0]) {
		case 0:
			/* Clear to end of line ??? or Backspace? */
			f = console->x;
			for( ; console->x < console->width; )
				vcPutCharColour(console, ' ', vt_fg, vt_bg);
			vcHideCursor(console);
			console->x = f;
			break;
		case 1:
			/* Clear to begin of line. */
			f = console->x;
			for( console->x = 0; console->x < f; )
				vcPutCharColour(console, ' ', vt_fg, vt_bg);
			break;
		case 2:
			/* Clear entire line. */
			f = console->x;
			for( console->x = 0; console->x < console->width;  )
				vcPutCharColour(console, ' ', vt_fg, vt_bg);
			vcHideCursor(console);
			console->x = f;
			break;
		}
		break;
	case 'J': /* Screen erasing */
	{
		fprintf(stdout, "Screen erasing (%d)\n", escparms[0]);
		switch (escparms[0]) {
		case 0:
			/* Clear to end of screen */
			//mc_wclreos(vt_win);
			//break;
		case 1:
			/* Clear to begin of screen. */
			//mc_wclrbos(vt_win);
			//break;
		case 2:
			/* Clear a window. */
			memset(console->screen->frameBuffer, BLACK,
				console->screen->width * console->screen->height);
			memset(console->screenBuffer, ' ', console->width * console->height);
#ifdef USE_ATTRIBUTE_BUFFER
			memset(console->attributeBuffer, 0x07, console->width * console->height);
#endif
			rfbMarkRectAsModified(console->screen, 0, 0, console->screen->width, console->screen->height);
			//mc_winclr(vt_win);
			break;
		}
		break;
	}
	case 'n': /* Requests / Reports */
		fprintf(stdout, "Requests / Reports\n");
		break;
	case 'c': /* Identify Terminal Type */
		fprintf(stdout, "Identify Terminal Type\n");
		break;
	case 'x': /* Request terminal parameters. */
		fprintf(stdout, "Request terminal parameters\n");
		break;
	case 's': /* Save attributes and cursor position */
		fprintf(stdout, "Save attributes and cursor position\n");
		break;
	case 'u': /* Restore them */
		fprintf(stdout, "Restore attributes and cursor position\n");
		break;
	case 'h':
		ansi_mode(1);
		break;
	case 'l':
		ansi_mode(0);
		break;
	case 'g': /* Clear tab stop(s) */
		fprintf(stdout, "Clear tab stop(s)\n");
		break;
	case 'm': /* Set attributes */
	{
		fprintf(stdout, "Set attributes\n");
//		attr = mc_wgetattr((vt_win));
		for (f = 0; f <= ptr; f++) {

			if (escparms[f] >= 30 && escparms[f] <= 37)
				vt_fg = (vt_fg & 15) + (escparms[f] - 30);
			if (escparms[f] >= 40 && escparms[f] <= 47)
				vt_bg = (vt_bg & 240) + (escparms[f] - 40);
			switch (escparms[f]) {
			case 0:
				attr = XA_NORMAL;
				vt_fg = WHITE;
				vt_bg = BLACK;
				break;
			case 1:
				attr |= XA_BOLD;
				break;
			case 4:
				attr |= XA_UNDERLINE;
				break;
			case 5:
				attr |= XA_BLINK;
				break;
			case 7:
				attr |= XA_REVERSE;
				break;
			case 22: /* Bold off */
				attr &= ~XA_BOLD;
				break;
			case 24: /* Not underlined */
				attr &=~XA_UNDERLINE;
				break;
			case 25: /* Not blinking */
				attr &= ~XA_BLINK;
				break;
			case 27: /* Not reverse */
				attr &= ~XA_REVERSE;
				break;
			case 39: /* Default fg color */
				vt_fg = 0x07;
				break;
			case 49: /* Default bg color */
				vt_bg = 0;
				break;
			}
		}
//		mc_wsetattr(vt_win, attr);
		break;
	}
	case 'L': /* Insert lines */
		if ((f = escparms[0]) == 0)
			f = 1;
		vcInsertLines( console, console->y, f );
		fprintf(stdout, "Insert lines\n");
		break;
	case 'M': /* Delete lines */
		if ((f = escparms[0]) == 0)
			f = 1;
		vcDeleteLines( console, console->y, f );
		fprintf(stdout, "Delete lines\n");
		break;
	case 'P': /* Delete Characters */
		if ((f = escparms[0]) == 0)
			f = 1;
		vcDeleteCharacters( console, f );
		fprintf(stdout, "Delete Characters\n");
		break;
	case '@': /* Insert Characters */
		if ((f = escparms[0]) == 0)
			f = 1;
		vcInsertCharacters( console, f );
		fprintf(stdout, "Insert Characters\n");
		break;
	case 'r': /* Set scroll region */
		y = escparms[0];
		x = escparms[1];
		if (y == 0)
			y = 1;
		if (x == 0 || x > console->height)
			x = console->height;
		if (y > newy2)
			y = newy2 + 1;
		console->sstart = y - 1;
		console->sheight = x;
		fprintf(stdout, "Set scroll region (%d, %d)\n", console->sstart, console->sheight);
		break;
	case 'i': /* Printing */
	case 'y': /* Self test modes */
	default:
		/* IGNORED */
		break;
	}
	/* Ok, our escape sequence is all done */
	esc_s = 0;
	ptr = 0;
	memset(escparms, 0, sizeof(escparms));
	return;
}

/* ESC [? ... [hl] seen. */
static void dec_mode(vncConsole *console, int on_off)
{
	int i;
	(void)on_off;
	for (i = 0; i <= ptr; i++) {
		switch (escparms[i]) {
		case 5: /* Visible Bell */
			if( on_off )
				rfbSendBell( console->screen );
			break;
		case 7: /* Auto wrap */
			fprintf(stdout, "Auto wrap");
			break;
		case 25: /* Cursor on/off */
			fprintf(stdout, "Cursor");
			if( on_off )
				vcDrawCursor( console );
			else
				vcHideCursor( console );
			console->dontDrawCursor = !on_off;
			break;
		default: /* Mostly set up functions */
			/* IGNORED */
			fprintf(stdout, "%d parameter IGNORED", escparms[i]);
			break;
		}
	}
	fprintf(stdout, " %s\n", on_off?"on":"off");
}

/*
 * ESC [ ? ... seen.
 */
static void state3(vncConsole *console, unsigned char c)
{
	/* See if a number follows */
	if (c >= '0' && c <= '9') {
		escparms[ptr] = 10*escparms[ptr] + c - '0';
		return;
	}
	switch (c) {
	case 'h':
		dec_mode(console, 1);
		break;
	case 'l':
		dec_mode(console, 0);
		break;
	case 'i': /* Printing */
	case 'n': /* Request printer status */
	default:
		/* IGNORED */
		break;
	}
	esc_s = 0;
	ptr = 0;
	memset(escparms, 0, sizeof(escparms));
	return;
}

/*
 * ESC # Seen.
 */
static void state6(unsigned char c)
{
	/* Double height, double width and selftests. */
	switch (c) {
	case '8':
		fprintf(stdout, "Selftest\n");
		break;
	default:
		/* IGNORED */
		break;
	}
	esc_s = 0;
}

