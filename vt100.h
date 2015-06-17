/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __VT100_H__
#define __VT100_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "console.h"

void vt_init(vncConsole *console);
void vt_out(vncConsole *console, unsigned char c);

#ifdef __cplusplus
}
#endif

#endif /* __VT100_H__ */
