/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rfb/rfb.h>

/*
 *  * Possible attributes.
 *   */
#define XA_NORMAL        0
#define XA_BLINK         1
#define XA_BOLD          2
#define XA_REVERSE       4
#define XA_STANDOUT      8
#define XA_UNDERLINE    16
#define XA_ALTCHARSET   32
#define XA_BLANK        64

/*
 *  * Possible colors
 *   */
#define BLACK           0
#define RED             1
#define GREEN           2
#define YELLOW          3
#define BLUE            4
#define MAGENTA         5
#define CYAN            6
#define WHITE           7

/* this is now the default */
#define USE_ATTRIBUTE_BUFFER

typedef struct vncConsole {
  /* width and height in cells (=characters) */
  int width, height;

  /* current position */
  int x,y;

  /* characters */
  char *screenBuffer;

#ifdef USE_ATTRIBUTE_BUFFER
  /* attributes: colours. If NULL, default to gray on black, else
     for each cell an unsigned char holds foreColour|(backColour<<4) */
  char *attributeBuffer;
#endif

  /* if this is set, the screen doesn't scroll. */
  rfbBool wrapBottomToTop;

  /* height and width of one character */
  int cWidth, cHeight;
  /* offset of characters */
  int xhot,yhot;
  /* scroll region */
  int sstart, sheight;

  /* colour */
  unsigned char foreColour,backColour;
  int8_t cx1,cy1,cx2,cy2;

  /* input buffer */
  char *inputBuffer;
  int inputCount;
  int inputSize;
  long selectTimeOut;
  rfbBool doEcho; /* if reading input, do output directly? */

  /* selection */
  char *selection;

  /* mouse */
  rfbBool wasRightButtonDown;
  rfbBool currentlyMarking;
  int markStart,markEnd;

  /* should text cursor be drawn? (an underscore at current position) */
  rfbBool cursorActive;
  rfbBool cursorIsDrawn;
  rfbBool dontDrawCursor; /* for example, while scrolling */

  rfbFontDataPtr font;
  rfbScreenInfoPtr screen;
} vncConsole, *vncConsolePtr;

#ifdef USE_ATTRIBUTE_BUFFER
vncConsolePtr vcGetConsole(int *argc,char **argv,
			   int width,int height,rfbFontDataPtr font,
			   rfbBool withAttributes);
#else
vncConsolePtr vcGetConsole(int *argc,char **argv,
			   int width,int height,rfbFontDataPtr font);
#endif
void vcDrawCursor(vncConsolePtr c);
void vcHideCursor(vncConsolePtr c);
void vcCheckCoordinates(vncConsolePtr c);

void vcPutChar(vncConsolePtr c,unsigned char ch);
void vcPrint(vncConsolePtr c,unsigned char* str);
void vcPrintF(vncConsolePtr c,char* format,...);

void vcPutCharColour(vncConsolePtr c,unsigned char ch,
		     unsigned char foreColour,unsigned char backColour);
void vcPrintColour(vncConsolePtr c,unsigned char* str,
		   unsigned char foreColour,unsigned char backColour);
void vcPrintFColour(vncConsolePtr c,unsigned char foreColour,
		    unsigned char backColour,char* format,...);

char vcGetCh(vncConsolePtr c);
char vcGetChar(vncConsolePtr c); /* blocking */
char *vcGetString(vncConsolePtr c,char *buffer,int maxLen);

void vcInsertCharacters(vncConsolePtr c, int n);
void vcDeleteCharacters(vncConsolePtr c, int n);
void vcInsertLines(vncConsolePtr c, int from, int count);
void vcDeleteLines(vncConsolePtr c, int from, int count);

void vcKbdAddEventProc(rfbBool down,rfbKeySym keySym,rfbClientPtr cl);
void vcPtrAddEventProc(int buttonMask,int x,int y,rfbClientPtr cl);
void vcSetXCutTextProc(char* str,int len, struct _rfbClientRec* cl);

void vcToggleMarkCell(vncConsolePtr c,int pos);
void vcUnmark(vncConsolePtr c);

void vcProcessEvents(vncConsolePtr c);

/* before using this function, hide the cursor */
void vcScroll(vncConsolePtr c,int lineCount);
void vcReset(vncConsolePtr c);

#ifdef __cplusplus
}
#endif

#endif /* __CONSOLE_H__ */
