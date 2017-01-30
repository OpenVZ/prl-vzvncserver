/*
 * console.c
 *
 * Copyright (C) 2003 Johannes E. Schindelin <Johannes.Schindelin@gmx.de>
 * Copyright (C) 2015-2017 Parallels IP Holdings GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <rfb/keysym.h>
#include "console.h"

#define MAX_CUT_TEXT_SYMBOLS 65535

unsigned char colourMap16[16*3]={
  /* 0 black       #000000 */ 0x00,0x00,0x00,
  /* 1 maroon      #800000 */ 0x80,0x00,0x00,
  /* 2 green       #008000 */ 0x00,0x80,0x00,
  /* 3 khaki       #808000 */ 0x80,0x80,0x00,
  /* 4 navy        #000080 */ 0x00,0x00,0x80,
  /* 5 purple      #800080 */ 0x80,0x00,0x80,
  /* 6 aqua-green  #008080 */ 0x00,0x80,0x80,
  /* 7 light grey  #c0c0c0 */ 0xc0,0xc0,0xc0,
  /* 8 dark grey   #808080 */ 0x80,0x80,0x80,
  /* 9 red         #ff0000 */ 0xff,0x00,0x00,
  /* a light green #00ff00 */ 0x00,0xff,0x00,
  /* b yellow      #ffff00 */ 0xff,0xff,0x00,
  /* c blue        #0000ff */ 0x00,0x00,0xff,
  /* d pink        #ff00ff */ 0xff,0x00,0xff,
  /* e light blue  #00ffff */ 0x00,0xff,0xff,
  /* f white       #ffffff */ 0xff,0xff,0xff
};

int MakeColourMap16(vncConsolePtr c)
{
  rfbColourMap* colourMap=&(c->screen->colourMap);
  if(colourMap->count)
    free(colourMap->data.bytes);
  colourMap->data.bytes= (unsigned char *)malloc(16*3);
  if (colourMap->data.bytes == NULL) {
    rfbLog("Unable to allocate mem for colourMap->data.bytes\n");
    return -1;
  }
  memcpy(colourMap->data.bytes,colourMap16,16*3);
  colourMap->count=16;
  colourMap->is16=FALSE;
  c->screen->serverFormat.trueColour=FALSE;
  return 0;
}

void vcDrawOrHideCursor(vncConsolePtr c)
{
  int i,j,w=c->screen->paddedWidthInBytes;
  char *b=c->screen->frameBuffer+c->y*c->cHeight*w+c->x*c->cWidth;
  for(j=c->cy1;j<c->cy2;j++)
    for(i=c->cx1;i<c->cx2;i++)
      b[j*w+i]^=0x0f;
  rfbMarkRectAsModified(c->screen,
			c->x*c->cWidth+c->cx1,c->y*c->cHeight+c->cy1,
			c->x*c->cWidth+c->cx2,c->y*c->cHeight+c->cy2);
  c->cursorIsDrawn=c->cursorIsDrawn?FALSE:TRUE;
}

void vcDrawCursor(vncConsolePtr c)
{
//	rfbLog("DrawCursor: %d,%d   is drawn: %d\n",c->x,c->y,c->cursorIsDrawn);
	if(!c->dontDrawCursor && !c->cursorIsDrawn && (c->y<c->height) && (c->x<c->width)) {
		/* rfbLog("DrawCursor: %d,%d\n",c->x,c->y); */
		vcDrawOrHideCursor(c);
	}
}

void vcHideCursor(vncConsolePtr c)
{
//	rfbLog("HideCursor: %d,%d   is drawn: %d\n",c->x,c->y,c->cursorIsDrawn);
	if(c->currentlyMarking)
		vcUnmark(c);
	if(c->cursorIsDrawn)
		vcDrawOrHideCursor(c);
}

void vcMakeSureCursorIsDrawn(rfbClientPtr cl)
{
	vncConsolePtr c = (vncConsolePtr)cl->screen->screenData;
	if (!c->dontDrawCursor)
		vcDrawCursor(c);
}

vncConsolePtr vcGetConsole(int *argc,char **argv,
			   int width,int height,rfbFontDataPtr font
#ifdef USE_ATTRIBUTE_BUFFER
			   ,rfbBool withAttributes
#endif
			   )
{
  vncConsolePtr c=(vncConsolePtr)malloc(sizeof(vncConsole));
  if (c == NULL) {
    rfbLog("Unable to allocate sizeof(vncConsole) = %zu mem.\n", sizeof(vncConsole));
    return NULL;
  }

  c->font=font;
  c->width=width;
  c->height=height;
  c->screenBuffer=(char*)malloc(width*height);
  if (c->screenBuffer == NULL) {
    rfbLog("Unable to allocate width*height mem for screenBuffer, width = %d, height = %d\n",
             width, height);
    return NULL;
  }
  memset(c->screenBuffer,' ',width*height);
#ifdef USE_ATTRIBUTE_BUFFER
  if(withAttributes) {
    c->attributeBuffer=(char*)malloc(width*height);
    if (c->attributeBuffer != NULL)
        memset(c->attributeBuffer,0x07,width*height);
    else
        rfbLog("Unable to allocate width*height mem for attrBuffer, width = %d, height = %d\n", 
                width, height);
  } else
    c->attributeBuffer=NULL;
#endif
  c->x=0;
  c->y=0;
  c->sstart = 0;
  c->sheight = c->height;
  c->wrapBottomToTop=FALSE;
  c->cursorActive=TRUE;
  c->cursorIsDrawn=FALSE;
  c->dontDrawCursor=FALSE;
  c->inputBuffer=(char*)malloc(1024);
  if (c->inputBuffer == NULL) {
    rfbLog("Unable to allocate mem for inputBuffer\n");
    return NULL;
  }
  
  c->inputSize=1024;
  c->inputCount=0;
  c->selection=0;
  c->selectTimeOut=40000; /* 40 ms */
  c->doEcho=TRUE;

  c->wasRightButtonDown=FALSE;
  c->currentlyMarking=FALSE;

  rfbWholeFontBBox(font,&c->xhot,&c->cHeight,&c->cWidth,&c->yhot);
  c->cWidth-=c->xhot;
  c->cHeight=-c->cHeight-c->yhot;

  /* text cursor */
  c->cx1=c->cWidth/8;
  c->cx2=c->cWidth*7/8;
  c->cy2=c->cHeight-1-c->yhot+c->cHeight/16;
  if(c->cy2>=c->cHeight)
    c->cy2=c->cHeight-1;
  c->cy1=c->cy2-c->cHeight/8;
  if(c->cy1<0)
    c->cy2=0;

  if(!(c->screen = rfbGetScreen(argc,argv,c->cWidth*c->width,c->cHeight*c->height,8,1,1)))
    return NULL;
  c->screen->screenData=(void*)c;
  c->screen->displayHook=vcMakeSureCursorIsDrawn;
  c->screen->frameBuffer=
    (char*)malloc(c->screen->width*c->screen->height);
  if (c->screen->frameBuffer == NULL) {
    rfbLog("Unable to allocate memory for screen frameBuffer, width = %d, height = %d\n",
        width, height);
    return NULL;
  }
  memset(c->screen->frameBuffer,c->backColour,
	 c->screen->width*c->screen->height);
  c->screen->kbdAddEvent=vcKbdAddEventProc;
  c->screen->ptrAddEvent=vcPtrAddEventProc;
  c->screen->setXCutText=vcSetXCutTextProc;

  if (MakeColourMap16(c))
    return NULL;
  c->foreColour=0x7;
  c->backColour=0;

  return(c);
}

#include <rfb/rfbregion.h>

/* before using this function, hide the cursor */
void vcScroll(vncConsolePtr c,int lineCount)
{
  if(lineCount==0)
    return;

  /* rfbLog("begin scroll\n"); */
//  vcHideCursor(c);
//  c->dontDrawCursor=TRUE;

  if(lineCount>=(c->sheight - c->sstart)
		  || lineCount<=- (c->sheight - c->sstart))
  {
	  // Nothing to really scroll - just clear a viewport
	  memset( c->screenBuffer + c->sstart * c->width,
			  '.',
			  ( c->sheight - c->sstart ) * c->width);
#ifdef USE_ATTRIBUTE_BUFFER
	  if( c->attributeBuffer )
		  memset( c->attributeBuffer + c->sstart * c->width,
				  0x07,
				  ( c->sheight - c->sstart ) * c->width);
#endif
	  rfbFillRect( c->screen,
				   0, c->sstart * c->cHeight,
				   c->screen->width, c->sheight * c->cHeight,
				   c->backColour);
	  return;
  }
  if(lineCount>0)
	  vcDeleteLines( c, c->sstart, lineCount );
  else
	  vcInsertLines( c, c->sstart, -lineCount );
}

void vcCheckCoordinates(vncConsolePtr c)
{
	if(c->x>=c->width) {
		c->x=0;
		c->y++;
	}
	if (c->y >= c->sheight) {
		if(c->wrapBottomToTop) {
			c->y = 0;
		} else {
			vcScroll(c, c->y + 1 - c->sheight);
			c->y = c->sheight - 1;
		}
	}
}

void vcInsertLines(vncConsolePtr c, int from, int f)
{
	int g, y, y2;

	if ( f > c->sheight - from)
		f = c->sheight - from;

	g = c->sheight - from - f;
	y = from * c->cHeight;
	vcHideCursor(c);
	if( g > 0 )
	{
		memmove(c->screenBuffer + (from + f) * c->width,
				c->screenBuffer + from * c->width,
				g * c->width);
#ifdef USE_ATTRIBUTE_BUFFER
		if( c->attributeBuffer )
			memmove(c->attributeBuffer + (from + f) * c->width,
					c->attributeBuffer + from * c->width,
					g * c->width);
#endif
		rfbDoCopyRect( c->screen,
					   0, (from + f) * c->cHeight,
					   c->screen->width,
					   c->sheight * c->cHeight,
					   0, f * c->cHeight);
	}
	// Fill inserted line(s) with spaces
	memset(c->screenBuffer + from * c->width,
		   ' ',
		   f * c->width);
#ifdef USE_ATTRIBUTE_BUFFER
	if(c->attributeBuffer)
		memset(c->screenBuffer + from * c->width,
			   0x07,
			   f * c->width);
#endif
	y2 = y + f * c->cHeight;
	if ( y2 > c->screen->height )
		y2 = c->screen->height;
	rfbFillRect( c->screen,
				 0, y,
				 c->screen->width, y2,
				 c->backColour);
}

void vcDeleteLines(vncConsolePtr c, int from, int f)
{
	int g, y;

	if ( f > c->sheight - from)
		f = c->sheight - from;

	g = c->sheight - from - f;
	y = from * c->cHeight;
	vcHideCursor(c);
	if( g > 0 )
	{
		memmove(c->screenBuffer + from * c->width,
				c->screenBuffer + (from + f) * c->width,
				g * c->width);
#ifdef USE_ATTRIBUTE_BUFFER
		if( c->attributeBuffer )
			memmove(c->attributeBuffer + from * c->width,
					c->attributeBuffer + (from + f) * c->width,
					g * c->width);
#endif
		rfbDoCopyRect( c->screen,
					   0, from * c->cHeight,
					   c->screen->width,
					   (c->sheight - f) * c->cHeight,
					   0, - f * c->cHeight);
	}
	// Fill inserted line(s) with spaces
	memset(c->screenBuffer + (from + g) * c->width,
		   ' ',
		   f * c->width);
#ifdef USE_ATTRIBUTE_BUFFER
	if(c->attributeBuffer)
		memset(c->screenBuffer + (from + g) * c->width,
			   0x07,
			   f * c->width);
#endif
	rfbFillRect( c->screen,
				 0, y + g * c->cHeight,
				 c->screen->width, c->sheight * c->cHeight,
				 c->backColour);
}

void vcDeleteCharacters(vncConsolePtr c, int f)
{
	int g, x, y;

	if (f > c->width)
		f = c->width;

	g = c->width - c->x - f;
	x = c->x * c->cWidth;
	y = c->y * c->cHeight;

	vcHideCursor(c);
	if (g > 0)
	{
		memmove(c->screenBuffer + c->y * c->width + c->x,
			   c->screenBuffer + c->y * c->width + c->x + f,
			   g);
#ifdef USE_ATTRIBUTE_BUFFER
		if( c->attributeBuffer )
			memmove(c->attributeBuffer + c->y * c->width + c->x,
				   c->attributeBuffer + c->y * c->width + c->x + f,
				   g);
#endif
		rfbDoCopyRect( c->screen,
					   x, y,
					   c->screen->width - f * c->cWidth, y + c->cHeight,
					   - f * c->cWidth, 0);
	}
	memset( c->screenBuffer + c->y * c->width + g, ' ', f );
#ifdef USE_ATTRIBUTE_BUFFER
	if( c->attributeBuffer )
		memset( c->attributeBuffer + c->y * c->width + g, 0x07, f );
#endif
	rfbFillRect( c->screen,
				 x + g * c->cWidth, y,
				 c->screen->width, y + c->cHeight,
				 c->backColour);
}

void vcInsertCharacters(vncConsolePtr c, int f)
{
	int g, x, y;

	g = c->width - c->x - f;
	if( g > 0 )
	{
		vcHideCursor(c);
		memmove(c->screenBuffer + c->y * c->width + c->x + f,
				c->screenBuffer + c->y * c->width + c->x,
				g);
#ifdef USE_ATTRIBUTE_BUFFER
		if( c->attributeBuffer )
			memmove(c->attributeBuffer + c->y * c->width + c->x + f,
					c->attributeBuffer + c->y * c->width + c->x,
					g);
#endif
		x = c->x * c->cWidth;
		y = c->y * c->cHeight;
		rfbDoCopyRect( c->screen,
					   x + f * c->cWidth, y,
					   c->screen->width,
					   y + c->cHeight,
					   f * c->cWidth, 0);
	}
	// TODO: Should we put f*' ' here or rely on the fact that client will provide
	// proper ones after insert request?
}

void vcPutChar(vncConsolePtr c,unsigned char ch)
{
#ifdef USE_ATTRIBUTE_BUFFER
  if(c->attributeBuffer) {
    unsigned char colour=c->attributeBuffer[c->x+c->y*c->width];
    vcPutCharColour(c,ch,colour&0x7,colour>>4);
  } else
#endif
    vcPutCharColour(c,ch,c->foreColour,c->backColour);
}

void vcPutCharColour(vncConsolePtr c,unsigned char ch,unsigned char foreColour,unsigned char backColour)
{
  rfbScreenInfoPtr s=c->screen;
  int j,x,y;

  vcHideCursor(c);
  if(ch<' ') {
    switch(ch) {
    case 13:
		c->x=0;
		break;
    case 8: /* BackSpace */
/*
      if(c->x>0) {
	c->x--;
	vcPutChar(c,' ');
	c->x--;
      }
*/
      break;
    case 10: /* return */
      c->x=0;
      c->y++;
      vcCheckCoordinates(c);
      break;
    case 9: /* tabulator */
      do {
	vcPutChar(c,' ');
      } while(c->x%8);
      break;
    default:
       rfbLog("putchar of unknown character: %c(%d).\n",ch,ch);
      vcPutChar(c,' ');
    }
  } else {
    vcCheckCoordinates(c);
#ifdef USE_ATTRIBUTE_BUFFER
    if(c->attributeBuffer)
      c->attributeBuffer[c->x+c->y*c->width]=foreColour|(backColour<<4);
#endif
    x=c->x*c->cWidth;
    y=c->y*c->cHeight;
    for(j=y+c->cHeight-1;j>=y;j--)
      memset(s->frameBuffer+j*s->width+x,backColour,c->cWidth);
    rfbDrawChar(s,c->font,
		x-c->xhot+(c->cWidth-rfbWidthOfChar(c->font,ch))/2,
		y+c->cHeight-c->yhot-1,
		ch,foreColour);
    c->screenBuffer[c->y*c->width+c->x]=ch;
    c->x++;
    rfbMarkRectAsModified(s,x,y-c->cHeight+1,x+c->cWidth,y+c->cHeight+1);
  }
}

void vcKbdAddEventProc(rfbBool down,rfbKeySym keySym,rfbClientPtr cl)
{
  vncConsolePtr c=(vncConsolePtr)cl->screen->screenData;
  if(down) {
    if(c->inputCount<c->inputSize) {
      if(/*keySym<0 ||*/ keySym>0xff) {
	if(keySym==XK_Return) keySym='\n';
	else if(keySym==XK_BackSpace) keySym=8;
        else if(keySym==XK_Tab) keySym=9;
	else keySym=0;
      }
      if(keySym>0) {
	if(keySym==8) {
	  if(c->inputCount>0)
	    c->inputCount--;
	} else
	  c->inputBuffer[c->inputCount++]=(char)keySym;
	if(c->doEcho)
	  vcPutChar(c,(unsigned char)keySym);
      }
    }
  }
}

void vcPtrAddEventProc(int buttonMask,int x,int y,rfbClientPtr cl)
{
  vncConsolePtr c=(vncConsolePtr)cl->screen->screenData;

  if(c->wasRightButtonDown) {
    if((buttonMask&4)==0) {
      if(c->selection) {
	char* s;
	for(s=c->selection;*s;s++) {
	  c->screen->kbdAddEvent(1,*s,cl);
	  c->screen->kbdAddEvent(0,*s,cl);
	}
      }
      c->wasRightButtonDown=0;
    }
  } else if(buttonMask&4)
    c->wasRightButtonDown=1;

  if(buttonMask&1) {
    int cx=x/c->cWidth,cy=y/c->cHeight,pos;
    if(cx<0) cx=0; else if(cx>=c->width) cx=c->width-1;
    if(cy<0) cy=0; else if(cy>=c->height) cy=c->height-1;
    pos=cy*c->width+cx;

    /* mark */
    if(!c->currentlyMarking) {
      c->currentlyMarking=TRUE;
      c->markStart=pos;
      c->markEnd=pos;
      vcToggleMarkCell(c,pos);
    } else {
      rfbLog("markStart: %d, markEnd: %d, pos: %d\n",
	      c->markStart,c->markEnd,pos);
      if(c->markEnd!=pos) {
	if(c->markEnd<pos) {
	  cx=c->markEnd; cy=pos;
	} else {
	  cx=pos; cy=c->markEnd;
	}
	if(cx<c->markStart) {
	  if(cy<c->markStart)
	    cy--;
	} else
	  cx++;
	while(cx<=cy) {
	  vcToggleMarkCell(c,cx);
	  cx++;
	}
	c->markEnd=pos;
      }
    }
  } else if(c->currentlyMarking) {
    int i,j;
    if(c->markStart<=c->markEnd) {
      i=c->markStart; j=c->markEnd+1;
    } else {
      i=c->markEnd; j=c->markStart;
    }
    if (j - i <= 0 || j - i > c->width * c->height) {
        rfbLog("vcPtrAddEventProc: something wrong with markStart and markEnd\n");
        return;
    }
    if(c->selection) free(c->selection);
    c->selection=(char*)malloc(j-i+1);
    if (c->selection == NULL) {
        rfbLog("Unable to allocate selection mem, j = %d, i = %d\n", i, j);
        return;
    }
    memcpy(c->selection,c->screenBuffer+i,j-i);
    c->selection[j-i]=0;
    vcUnmark(c);
    rfbGotXCutText(c->screen,c->selection,j-i);
  }
  rfbDefaultPtrAddEvent(buttonMask,x,y,cl);
}

void vcSetXCutTextProc(char* str,int len, struct _rfbClientRec* cl)
{
  if (str == NULL || len <= 0) {
    rfbLog("vcSetXCutTextProc: str == NULL or len == 0");
    return;
  }
  if (len > MAX_CUT_TEXT_SYMBOLS) {
    rfbLog("vcSetXCutTextProc: too long selection");
    return;
  }
  vncConsolePtr c=(vncConsolePtr)cl->screen->screenData;

  if(c->selection) free(c->selection);
  c->selection=(char*)malloc(len+1);
  if (c->selection == NULL) {
    rfbLog("Unable to allocate selection mem, len = %d", len);
    return;
  }
  memcpy(c->selection,str,len);
  c->selection[len]=0;
}

void vcToggleMarkCell(vncConsolePtr c,int pos)
{
  int x=(pos%c->width)*c->cWidth,
    y=(pos/c->width)*c->cHeight;
  int i,j;
  rfbScreenInfoPtr s=c->screen;
  char *b=s->frameBuffer+y*s->width+x;
  for(j=0;j<c->cHeight;j++)
    for(i=0;i<c->cWidth;i++)
      b[j*s->width+i]^=0x0f;
  rfbMarkRectAsModified(c->screen,x,y,x+c->cWidth,y+c->cHeight);
}

void vcUnmark(vncConsolePtr c)
{
  int i,j;
  c->currentlyMarking=FALSE;
  if(c->markStart<=c->markEnd) {
    i=c->markStart; j=c->markEnd+1;
  } else {
    i=c->markEnd; j=c->markStart;
  }
  for(;i<j;i++)
    vcToggleMarkCell(c,i);
}

/* before using this function, hide the cursor */
void vcReset(vncConsolePtr c)
{
	int y1,y2;
	rfbScreenInfoPtr s = c->screen;

	y1 = s->height - c->height * c->cHeight;
	y2 = s->height;
	memset(s->frameBuffer + y1 * s->width, c->backColour, (y2-y1) * s->width);
	rfbMarkRectAsModified(s, 0, y1-c->cHeight, s->width, y2);
	memset(c->screenBuffer + y1/c->cHeight*c->width, ' ',
		(y2-y1)/c->cHeight*c->width);
#ifdef USE_ATTRIBUTE_BUFFER
	if(c->attributeBuffer)
		memset(c->attributeBuffer+y1/c->cHeight*c->width,0x07,
			(y2-y1)/c->cHeight*c->width);
#endif
	c->x = 0;
	c->y = 0;
	c->sstart = 0;
	c->sheight = c->height;
}

