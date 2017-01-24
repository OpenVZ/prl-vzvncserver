/*
 * util.h
 *
 * Copyright (C) 2015-2017 Parallels IP Holdings GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#ifdef __cplusplus
extern "C" {
#endif

// error codes
#define VZ_VNC_ERR_PARAM	1
#define VZ_VNC_ERR_SOCK		2
#define VZ_VNC_ERR_RFB		3
#define VZ_VNC_ERR_SYSTEM	4

// log levels
#define VZ_VNC_ERR		0
#define VZ_VNC_WARN		1
#define VZ_VNC_INFO		2
#define VZ_VNC_DEBUG		3

void init_logger(const char * log_file, int log_level);
void vzvnc_logger(int log_level, const char * format, ...);
int vzvnc_error(int err_code, const char * format, ...);

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_H__ */
