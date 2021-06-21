/*
 * util.h
 *
 * Copyright (c) 2015-2017, Parallels International GmbH
 * Copyright (c) 2017-2019 Virtuozzo International GmbH. All rights reserved.
 *
 * This file is part of OpenVZ. OpenVZ is free software; you can redistribute
 * it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Our contact details: Virtuozzo International GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
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

void init_logger(const char * log_file, int log_level, int is_verbose);
void vzvnc_logger(int log_level, const char * format, ...);
int vzvnc_error(int err_code, const char * format, ...);

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_H__ */
