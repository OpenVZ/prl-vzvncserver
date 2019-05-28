/*
 * main.c
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

#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>

#include <rfb/keysym.h>
#include <rfb/rfb.h>
#include "util.h"
#include "console.h"
#include "vga.h"
#include "vt100.h"

#include <vzctl/libvzctl.h>
#include <linux/vzcalluser.h>

#ifndef TIOSAK
#define TIOSAK  _IO('T', 0x66)  /* "Secure Attention Key" */
#endif

#define _WITH_MUTEX_
#define MAX_PASSWD	4096
#define MAX_TTY		12

static char progname[NAME_MAX + 1];
static char title[128];

static int handle_rfb_event = 0;
static sig_atomic_t shutting_down = 0;

#ifdef _WITH_MUTEX_
static pthread_mutex_t mutex;
#endif
static int tty_fd = -1;

static vncConsole *console = NULL;

struct linuxConsoleSequence
{
	rfbKeySym keySym;
	const char * sequence;
} linuxConsoleSequences[] = {
{ XK_Escape, "\e" },
{ XK_Tab, "\t" },
{ XK_Return, "\r" },
{ XK_BackSpace, "\177" },
{ XK_Home, "\e[1~" },
{ XK_KP_Home, "\e[1~" },
{ XK_Insert, "\e[2~" },
{ XK_KP_Insert, "\e[2~" },
{ XK_Delete, "\e[3~" },
{ XK_KP_Delete, "\e[3~" },
{ XK_End, "\e[4~" },
{ XK_KP_End, "\e[4~" },
{ XK_Page_Up, "\e[5~" },
{ XK_KP_Page_Up, "\e[5~" },
{ XK_Page_Down, "\e[6~" },
{ XK_KP_Page_Down, "\e[6~" },
{ XK_Up, "\e[A" },
{ XK_KP_Up, "\e[A" },
{ XK_Down, "\e[B" },
{ XK_KP_Down, "\e[B" },
{ XK_Right, "\e[C" },
{ XK_KP_Right, "\e[C" },
{ XK_Left, "\e[D" },
{ XK_KP_Left, "\e[D" },
{ XK_KP_Begin, "\e[G" },
{ XK_F1, "\e[[A" },
{ XK_F2, "\e[[B" },
{ XK_F3, "\e[[C" },
{ XK_F4, "\e[[D" },
{ XK_F5, "\e[[E" },
{ XK_F6, "\e[17~" },
{ XK_F7, "\e[18~" },
{ XK_F8, "\e[19~" },
{ XK_F9, "\e[20~" },
{ XK_F10, "\e[21~" },
{ XK_F11, "\e[23~" },
{ XK_F12, "\e[24~" },
{ XK_F13, "\e[25~" },
{ XK_F14, "\e[26~" },
{ XK_F15, "\e[28~" },
{ XK_F16, "\e[29~" },
{ XK_F17, "\e[31~" },
{ XK_F18, "\e[32~" },
{ XK_F19, "\e[33~" },
{ XK_F20, "\e[34~" },
{ 0, "" },
};

void do_key(rfbBool down,rfbKeySym keySym,rfbClientPtr cl)
{
	static char isControl = 0;
	(void)cl;

	if(down) {
		if(keySym==XK_Control_L || keySym==XK_Control_R)
			isControl++;
		else if(tty_fd>=0)
		{
			int i;
			if(isControl) {
				if(keySym>='a' && keySym<='z')
					keySym-='a'-1;
				else if(keySym>='A' && keySym<='Z')
					keySym-='A'-1;
				else
					keySym=0xffff;
			} else
				for(i = 0; linuxConsoleSequences[i].keySym; i++)
					if( linuxConsoleSequences[i].keySym == keySym )
					{
						if (write(tty_fd,
								  linuxConsoleSequences[i].sequence,
								  strlen(linuxConsoleSequences[i].sequence)) == -1)
							perror("write()");
						return;
					}

			if(keySym<0x100)
			{
				if (write(tty_fd, &keySym, 1) == -1)
					perror( "write()");
			}
		}
	} else if(keySym==XK_Control_L || keySym==XK_Control_R)
		if(isControl>0)
			isControl--;
}

static void do_client_disconnect(rfbClientPtr cl)
{
    syslog(LOG_INFO, "%s: Client %s disconnected", title, cl->host);
}

static enum rfbNewClientAction do_client_connect(rfbClientPtr cl)
{
    cl->clientGoneHook = do_client_disconnect;
    syslog(LOG_INFO, "%s: Client %s connected", title, cl->host);

    return RFB_CLIENT_ACCEPT;
}


/* these colours are from linux kernel drivers/char/console.c */
unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
				       8,12,10,14, 9,13,11,15 };
/* the default colour table, for VGA+ colour systems */
int default_red[] = {0x00,0xaa,0x00,0xaa,0x00,0xaa,0x00,0xaa,
    0x55,0xff,0x55,0xff,0x55,0xff,0x55,0xff};
int default_grn[] = {0x00,0x00,0xaa,0x55,0x00,0x00,0xaa,0xaa,
    0x55,0x55,0xff,0xff,0x55,0x55,0xff,0xff};
int default_blu[] = {0x00,0x00,0x00,0x00,0xaa,0xaa,0xaa,0xaa,
    0x55,0x55,0x55,0x55,0xff,0xff,0xff,0xff};

static int system_console = 0;

static void _shutdown()
{
	if (!shutting_down) {
		shutting_down = 1;
		if (tty_fd != -1) {
			if( !system_console )
				ioctl(tty_fd, TIOSAK);
			close(tty_fd);
			tty_fd = -1;
		}
	}
}

static void sigterm_handler(int sig)
{
// TODO - close session ^D	write(tty_fd, " ", 1);
	vzvnc_logger(VZ_VNC_INFO, "signal %d, exited", sig);
	_shutdown();
}

static void *rfb_event_handler(void* data)
{
	long rc = 0;
	vncConsolePtr console = (vncConsolePtr)data;

	while (handle_rfb_event) {
#ifdef _WITH_MUTEX_
		usleep(1000);
		if (pthread_mutex_lock(&mutex)) {
			rc = vzvnc_error(VZ_VNC_ERR_SYSTEM, "pthread_mutex_lock(): %m");
			break;
		}
#endif
		rfbProcessEvents(console->screen, console->selectTimeOut);
#ifdef _WITH_MUTEX_
		pthread_mutex_unlock(&mutex);
#endif
	}
	pthread_exit((void *)rc);
}

static void usage(int code)
{
	fprintf(stderr, PRODUCT_NAME_SHORT " VNC server for Containers\n");
	fprintf(stderr, "Usage: %s [options] Container ID\n", progname);
	fprintf(stderr,"  Options:\n");
	fprintf(stderr,"    -l/--listen ADDR    listen for connections only on network interface with\n");
	fprintf(stderr,"                        addr ipaddr. '-listen localhost' and hostname work too.(-listen) \n");
	fprintf(stderr,"    -p/--port N         TCP port for RFB protocol (-rfbport)\n");
	fprintf(stderr,"       --auto-port      select free TCP port for RFB protocol in range\n");
	fprintf(stderr,"       --min-port       set lower limit of range for --auto-port option (includes)\n");
	fprintf(stderr,"       --max-port       set upper limit of range for --auto-port option (includes)\n");
	fprintf(stderr,"       --connect-timeout set websocket connect timeout\n");
	fprintf(stderr,"       --send-timeout   set websocket send timeout\n");
	fprintf(stderr,"    -d/--debug LEVEL    set debug level for logs (1-3, 2 as default)\n");
	fprintf(stderr,"    -v/--verbose        set verbose level for stdout/stderr\n");
	fprintf(stderr,"    -c/--sslcert CFILE  specify SSL certificate file for websockets\n");
	fprintf(stderr,"    -s/--system         use tty1 (aka /dev/console) to connect with\n");
	fprintf(stderr,"    -k/--sslkey KFILE   specify SSL key file for websockets\n");
	fprintf(stderr,"    -h/--help           show usage and exit\n");
	exit(code);
}

struct options {
	char *addr;
	char *port;
	char *sslkey;
	char *sslcert;
	unsigned auto_port;
	unsigned max_port;
	unsigned min_port;
	unsigned debug_level;
	char passwd;
	int is_verbose;
	int ws_connect_timeout;
	int ws_send_timeout;
};

static int parse_cmd_line(int argc, char *argv[], struct options *opts)
{
	int c, err;
	char *p;
	struct option options[] =
	{
		{"listen", required_argument, NULL, 'l'},
		{"port", required_argument, NULL, 'p'},
		{"auto-port", no_argument, NULL, 1},
		{"min-port", required_argument, NULL, 2},
		{"max-port", required_argument, NULL, 3},
		{"connect-timeout", required_argument, NULL, 5},
		{"send-timeout", required_argument, NULL, 6},
		{"passwd", no_argument, NULL, 4},
		{"debug", required_argument, NULL, 'd'},
		{"sslkey", required_argument, NULL, 'k'},
		{"sslcert", required_argument, NULL, 'c'},
		{"system", no_argument, NULL, 's'},
		{"verbose", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{ NULL, 0, NULL, 0 }
	};
	struct vzctl_config * cfg = vzctl2_conf_open(VZ_GLOBAL_CFG, VZCTL_CONF_SKIP_GLOBAL, &err);

	memset((void *)opts, 0, sizeof(struct options));

	if (cfg)
	{
		const char * out;
		if (!vzctl2_conf_get_param(cfg, "WEBSOCKET_CONNECT_TIMEOUT", &out) && out)
		{
			int ws_connect_timeout = strtol(out, &p, 10);
			if (*p == '\0')
				opts->ws_connect_timeout = ws_connect_timeout;
		}
		if (!vzctl2_conf_get_param(cfg, "WEBSOCKET_SEND_TIMEOUT", &out) && out)
		{
			int ws_send_timeout = strtol(out, &p, 10);
			if (*p == '\0')
				opts->ws_send_timeout = ws_send_timeout;
		}
		vzctl2_conf_close(cfg);
	}

	while (1)
	{
		c = getopt_long(argc, argv, "sl:p:d:hvk:c:", options, NULL);
		if (c == -1)
			break;
		switch (c)
		{
		case 'l':
			if (optarg == NULL)
				usage(VZ_VNC_ERR_PARAM);
			opts->addr = optarg;
			break;
		case 'p':
			if (optarg == NULL)
				usage(VZ_VNC_ERR_PARAM);
			opts->port = optarg;
			break;
		case 'd':
			if (optarg == NULL)
				usage(VZ_VNC_ERR_PARAM);
			opts->debug_level = strtoul(optarg, &p, 10);
			if (*p != '\0')
				usage(VZ_VNC_ERR_PARAM);
			break;
		case 'k':
			if (optarg == NULL)
				usage(1);
			opts->sslkey = optarg;
			break;
		case 'c':
			if (optarg == NULL)
				usage(1);
			opts->sslcert = optarg;
			break;
		case 's':
			system_console = 1;
			break;
		case 'v':
			opts->is_verbose = 1;
			break;
		case 1:
			opts->auto_port = 1;
			break;
		case 2:
			if (optarg == NULL)
				usage(VZ_VNC_ERR_PARAM);
			opts->min_port = strtoul(optarg, &p, 10);
			if (*p != '\0')
				usage(VZ_VNC_ERR_PARAM);
			break;
		case 3:
			if (optarg == NULL)
				usage(VZ_VNC_ERR_PARAM);
			opts->max_port = strtoul(optarg, &p, 10);
			if (*p != '\0')
				usage(VZ_VNC_ERR_PARAM);
			break;
		case 4:
			opts->passwd = 1;
			break;
		case 5:
			if (optarg == NULL)
				usage(VZ_VNC_ERR_PARAM);
			opts->ws_connect_timeout = strtol(optarg, &p, 10);
			if (*p != '\0')
				usage(VZ_VNC_ERR_PARAM);
			break;
		case 6:
			if (optarg == NULL)
				usage(VZ_VNC_ERR_PARAM);
			opts->ws_send_timeout = strtol(optarg, &p, 10);
			if (*p != '\0')
				usage(VZ_VNC_ERR_PARAM);
			break;
		case 'h':
			usage(VZ_VNC_ERR_PARAM);
			exit(0);
		default:
			usage(VZ_VNC_ERR_PARAM);
			exit(1);
		}
	}
	return 0;
}

int main(int argc,char **argv)
{
	int rc = 0;

	int width = 80, height = 24;
	char buf;
	int i;

	const char *vzctl = "/dev/vzctl";
	char path[PATH_MAX+1];
	int debug_level = VZ_VNC_INFO;

	int dev;
	char name[512];
	struct vzctl_ve_configure c;
	struct vzctl_env_handle *h = NULL;
	ctid_t ctid = {};
	ssize_t sz;
	pthread_t thread;
	int rfbArgc = 3;
	// default VNS addr & port
	char *rfbArgv[15] = {argv[0], (char *)"-listen", (char *)"0.0.0.0", NULL, NULL, NULL};
	struct options opts;
	char passwd[MAX_PASSWD];
	const char *passwds[] = {passwd, 0};
	time_t now;
	struct tm * timeinfo;

	strncpy(progname, basename(argv[0]), sizeof(progname));
	openlog(progname, LOG_CONS, LOG_DAEMON);

	parse_cmd_line(argc, argv, &opts);
	if (optind >= argc)
		usage(VZ_VNC_ERR_PARAM);
	if (opts.port && opts.auto_port)
		return vzvnc_error(VZ_VNC_ERR_PARAM,
			"both parameters --port and --auto-port were specified");

	snprintf(path, sizeof(path), "/var/log/%s", progname);
	mkdir(path, 0755);
	time(&now);
	timeinfo = localtime(&now);
	snprintf(path, sizeof(path), "/var/log/%s/%s-%d%02d%02d.log", progname, argv[optind],
			timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);

	vzctl2_init_log("prl_vzvncserver");

	if ((rc = vzctl2_lib_init()))
		return vzvnc_error(VZ_VNC_ERR_SYSTEM, "Failed to initialize libvzctl: %d", rc);

	if (vzctl2_convertstr(argv[optind], name, sizeof(name)))
		usage(VZ_VNC_ERR_PARAM);

	if (vzctl2_get_envid_by_name(name, ctid) &&
			vzctl2_parse_ctid(argv[optind], ctid))
	{
		rc = vzvnc_error(VZ_VNC_ERR_PARAM, "Invalid ctid is specified: %s\n", argv[optind]);
		usage(rc);
	}

	signal(SIGINT, sigterm_handler);
	signal(SIGTERM, sigterm_handler);

#ifdef _WITH_MUTEX_
	if (pthread_mutex_init(&mutex, NULL))
		return vzvnc_error(VZ_VNC_ERR_SYSTEM, "pthread_mutex_init(): %m");
#endif

	dev = open(vzctl, O_RDONLY);
	if (dev < 0) {
		rc = vzvnc_error(VZ_VNC_ERR_SYSTEM, "open(%s): %m", vzctl);
		goto cleanup_0;
	}

	h = vzctl2_env_open(ctid, 0, &rc);
	if (rc)
		goto cleanup_0;

	c.veid = vzctl2_env_get_veid(h);
	c.key = VE_CONFIGURE_OPEN_TTY;
	c.size = 0;

	for( c.val = system_console? 0 : 1;
		 c.val < MAX_TTY; c.val++ )
	{
		if( (tty_fd = ioctl(dev, VZCTL_VE_CONFIGURE, &c)) >= 0)
			break;

		if( system_console )
		{
			rc = vzvnc_error(VZ_VNC_ERR_SYSTEM,
							 "Setting up system console failed with: %m");
			close(dev);
			goto cleanup_0;
		}
		vzvnc_error( VZ_VNC_DEBUG, "ioctl(VZCTL_VE_CONFIGURE) for tty%d: %m", c.val+1 );
	}

	close(dev);
	if (tty_fd < 0)
	{
		rc = vzvnc_error(VZ_VNC_ERR_SYSTEM, "All %d tty devices are busy in CT %s. Exiting...", MAX_TTY, ctid);
		goto cleanup_0;
	}

	if (c.val > 1)
	{
        pid_t pid;
        int status;
        char tty_buf[3]; //MAX_TTY == 12, maximum - 2 chars

        sprintf(tty_buf, "%u", c.val + 1);

        char *args[] = {"/usr/sbin/vzctl", "console", ctid, "--start", tty_buf, NULL};

        pid = fork();
        if (pid == -1)
        {
            rc = vzvnc_error( VZ_VNC_ERR_SYSTEM, "Unable to start vzctl console: fork failed"); 
            goto cleanup_0;
        }
        else if (pid > 0)
        {
            if (waitpid(pid, &status, 0) != pid)
            {
                rc = vzvnc_error( VZ_VNC_ERR_SYSTEM, "Unable to start vzctl console: waitpid failed");
                goto cleanup_0;
            }
            if (WIFEXITED(status) && WEXITSTATUS(status))
            {
                rc = vzvnc_error( VZ_VNC_ERR_SYSTEM, "Unable to start vzctl console: program returned %d", status);
                goto cleanup_0;
            }
        }
        else
        {
            execv(args[0], args);
            exit(1);
        }
	}

	snprintf(title, sizeof(title), "CT %s tty%d", ctid, c.val + 1);

	/* console init */
	if (opts.addr)
		rfbArgv[2] = opts.addr;

	if (opts.port) {
		rfbArgv[rfbArgc++] = (char *)"-rfbport";
		rfbArgv[rfbArgc++] = opts.port;
		rfbArgv[rfbArgc++] = (char *)"-rfbportv6";
		rfbArgv[rfbArgc++] = opts.port;
		rfbArgv[rfbArgc] = NULL;
	}

	vzvnc_logger(VZ_VNC_INFO, "CT %s, addr %s port %s", ctid, rfbArgv[2], rfbArgv[4]);

	if (opts.sslkey)
	{
		rfbArgv[rfbArgc++] = (char *)"-sslkeyfile";
		rfbArgv[rfbArgc++] = opts.sslkey;
		rfbArgv[rfbArgc] = NULL;
	}

	if (opts.sslcert)
	{
		rfbArgv[rfbArgc++] = (char *)"-sslcertfile";
		rfbArgv[rfbArgc++] = opts.sslcert;
		rfbArgv[rfbArgc] = NULL;
	}

	if ((console = vcGetConsole(&rfbArgc, rfbArgv, width, height, &vgaFont
#ifdef USE_ATTRIBUTE_BUFFER
		,TRUE
#endif
	)) == NULL) {
		rc = vzvnc_error(VZ_VNC_ERR_RFB, "rfbGetConsole() error");
		goto cleanup_0;
	}

	if (opts.auto_port) {
		console->screen->autoPort = TRUE;
		if (opts.min_port)
			console->screen->minPort = opts.min_port;
		if (opts.max_port)
			console->screen->maxPort = opts.max_port;
	}

	if (opts.ws_connect_timeout)
		console->screen->wsClientConnect = opts.ws_connect_timeout;

	rfbLog("Websocket client connect timeout: %d ms\n", console->screen->wsClientConnect);

	if (opts.ws_send_timeout)
		console->screen->wsClientSend = opts.ws_send_timeout;

	rfbLog("Websocket client send timeout: %d ms\n", console->screen->wsClientSend);

	if (opts.passwd) {
		memset(passwd, 0, MAX_PASSWD);
		fread(passwd, 1, MAX_PASSWD, stdin);
		if (!feof(stdin)) {
			rc = vzvnc_error(VZ_VNC_ERR_PARAM, "Too long password");
			goto cleanup_0;
		}

		if(ferror(stdin)) {
			rc = vzvnc_error(VZ_VNC_ERR_PARAM, "Error reading password from STDIN");
			goto cleanup_0;
		}

		console->screen->passwordCheck = rfbCheckPasswordByList;
		console->screen->authPasswdData = passwds;
	}

	rfbInitServer(console->screen);
	init_logger(path, debug_level, opts.is_verbose);
	if (console->screen->listenSock < 0)
	{
		rc = vzvnc_error(VZ_VNC_ERR_SOCK, "Unable to open tcp port for listen for CT %s", ctid);
		goto cleanup_0;
	}

	for (i=0;i<16;i++) {
		console->screen->colourMap.data.bytes[i*3+0]=default_red[color_table[i]];
		console->screen->colourMap.data.bytes[i*3+1]=default_grn[color_table[i]];
		console->screen->colourMap.data.bytes[i*3+2]=default_blu[color_table[i]];
	}
	console->screen->desktopName = title;
	console->screen->kbdAddEvent = do_key;
	console->screen->newClientHook = do_client_connect;
	console->selectTimeOut = 100000;
	console->wrapBottomToTop = FALSE;
	console->cursorActive = TRUE;

	handle_rfb_event = 1;
	if (pthread_create(&thread, NULL, rfb_event_handler, (void *)console) < 0) {
		rc = vzvnc_error(VZ_VNC_ERR_SYSTEM, "phtread_create(): %m");
		goto cleanup_0;
	}

	vcHideCursor(console);
	vt_init(console);

	while (rfbIsActive(console->screen) && !shutting_down) {
		sz = read(tty_fd, &buf, sizeof(buf));
		if (sz == -1) {
			rc = vzvnc_error(VZ_VNC_ERR_SYSTEM, "read(): %m");
			goto cleanup_1;
		}
#ifdef _WITH_MUTEX_
		// lock mutex
		if (pthread_mutex_lock(&mutex)) {
			rc = vzvnc_error(VZ_VNC_ERR_SYSTEM, "pthread_mutex_lock(): %m");
			goto cleanup_1;
		}
#endif
		vt_out(console, buf);
#ifdef _WITH_MUTEX_
		pthread_mutex_unlock(&mutex);
#endif
	}

cleanup_1:
	handle_rfb_event = 0;
	pthread_join(thread, NULL);
cleanup_0:
	if (console != NULL)
		rfbShutdownServer(console->screen, 1);
#ifdef _WITH_MUTEX_
	pthread_mutex_destroy(&mutex);
#endif
	_shutdown();

	return rc;
}
