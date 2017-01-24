/*
 * util.c
 *
 * Copyright (C) 2015-2017 Parallels IP Holdings GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <error.h>
#include <time.h>

#include "util.h"

static int loglevel = 0;
static char *logfile = NULL;

void init_logger(const char * log_file, int log_level)
{
	if (logfile)
		free(logfile);
	logfile = strdup(log_file);
	loglevel = log_level;
}

int get_loglevel()
{
	return loglevel;
}

static char *get_date()
{
	struct tm *p_tm_time;
	time_t ptime;
	char str[100];

	*str = 0;
	ptime = time(NULL);
	p_tm_time = localtime(&ptime);
	strftime(str, sizeof(str), "%Y-%m-%dT%T%z", p_tm_time);

	return strdup(str);
}

static void write_log_rec(int log_level, const char *format, va_list ap)
{
	/* Put log message in log file and debug messages with
	 * level <= DEBUG_LEVEL to stderr
	 * Log message format: date script : message :
	 * err_message(based on errno)
	 * date script : message : err_message(based on errno)
	 * errno passed in the function as
	 */
	FILE *log_file;
	FILE *out;
	char *p;

	if (loglevel < log_level)
		return;

	if ((log_level == VZ_VNC_ERR) || (log_level == VZ_VNC_WARN))
		out = stderr;
	else
		out = stdout;

	p = get_date();
	/* Print formatted message */
	va_list ap_save;
	va_copy(ap_save, ap);
	if (log_level == 0)
		fprintf(out, "Error: ");
	vfprintf(out, format, ap_save);
	va_end(ap_save);
	fprintf(out, "\n");
	if (logfile) {
		if ((log_file = fopen(logfile, "a"))) {
			fprintf(log_file, "%s : ", p);
			if (log_level == 0)
				fprintf(log_file, "Error: ");
			vfprintf(log_file, format, ap);

			fprintf(log_file, "\n");
			fclose(log_file);
		}
	}
	fflush(out);
	if (p != NULL) free(p);
}

void vzvnc_logger(int log_level, const char * format, ...)
{
	va_list ap;
	va_start(ap, format);
	write_log_rec(log_level, format, ap);
	va_end(ap);
}

int vzvnc_error(int err_code, const char * format, ...)
{
	va_list ap;
	va_start(ap, format);
	write_log_rec(VZ_VNC_ERR, format, ap);
	va_end(ap);
	return err_code;
}

