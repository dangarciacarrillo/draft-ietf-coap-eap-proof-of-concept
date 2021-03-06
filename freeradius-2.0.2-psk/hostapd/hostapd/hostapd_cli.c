/*
 * hostapd - command line interface for hostapd daemon
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#include <dirent.h>

#include "wpa_ctrl.h"
#include "common.h"
#include "version.h"


static const char *hostapd_cli_version =
"hostapd_cli v" VERSION_STR "\n"
"Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi> and contributors";


static const char *hostapd_cli_license =
"This program is free software. You can distribute it and/or modify it\n"
"under the terms of the GNU General Public License version 2.\n"
"\n"
"Alternatively, this software may be distributed under the terms of the\n"
"BSD license. See README and COPYING for more details.\n";

static const char *hostapd_cli_full_license =
"This program is free software; you can redistribute it and/or modify\n"
"it under the terms of the GNU General Public License version 2 as\n"
"published by the Free Software Foundation.\n"
"\n"
"This program is distributed in the hope that it will be useful,\n"
"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
"GNU General Public License for more details.\n"
"\n"
"You should have received a copy of the GNU General Public License\n"
"along with this program; if not, write to the Free Software\n"
"Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA\n"
"\n"
"Alternatively, this software may be distributed under the terms of the\n"
"BSD license.\n"
"\n"
"Redistribution and use in source and binary forms, with or without\n"
"modification, are permitted provided that the following conditions are\n"
"met:\n"
"\n"
"1. Redistributions of source code must retain the above copyright\n"
"   notice, this list of conditions and the following disclaimer.\n"
"\n"
"2. Redistributions in binary form must reproduce the above copyright\n"
"   notice, this list of conditions and the following disclaimer in the\n"
"   documentation and/or other materials provided with the distribution.\n"
"\n"
"3. Neither the name(s) of the above-listed copyright holder(s) nor the\n"
"   names of its contributors may be used to endorse or promote products\n"
"   derived from this software without specific prior written permission.\n"
"\n"
"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
"\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
"LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
"A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n"
"OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
"SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
"LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
"DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
"THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
"(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
"OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
"\n";

static const char *commands_help =
"Commands:\n"
"   mib                  get MIB variables (dot1x, dot11, radius)\n"
"   sta <addr>           get MIB variables for one station\n"
"   all_sta              get MIB variables for all stations\n"
"   new_sta <addr>       add a new station\n"
#ifdef CONFIG_WPS
"   wps_pin <uuid> <pin> add WPS Enrollee PIN (Device Password)\n"
"   wps_pbc              indicate button pushed to initiate PBC\n"
#endif /* CONFIG_WPS */
"   help                 show this usage help\n"
"   interface [ifname]   show interfaces/select interface\n"
"   level <debug level>  change debug level\n"
"   license              show full hostapd_cli license\n"
"   quit                 exit hostapd_cli\n";

static struct wpa_ctrl *ctrl_conn;
static int hostapd_cli_quit = 0;
static int hostapd_cli_attached = 0;
static const char *ctrl_iface_dir = "/var/run/hostapd";
static char *ctrl_ifname = NULL;


static void usage(void)
{
	fprintf(stderr, "%s\n", hostapd_cli_version);
	fprintf(stderr, 
		"\n"	
		"usage: hostapd_cli [-p<path>] [-i<ifname>] [-hv] "
		"[command..]\n"
		"\n"
		"Options:\n"
		"   -h           help (show this usage text)\n"
		"   -v           shown version information\n"
		"   -p<path>     path to find control sockets (default: "
		"/var/run/hostapd)\n"
		"   -i<ifname>   Interface to listen on (default: first "
		"interface found in the\n"
		"                socket path)\n\n"
		"%s",
		commands_help);
}


static struct wpa_ctrl * hostapd_cli_open_connection(const char *ifname)
{
	char *cfile;
	int flen;

	if (ifname == NULL)
		return NULL;

	flen = strlen(ctrl_iface_dir) + strlen(ifname) + 2;
	cfile = malloc(flen);
	if (cfile == NULL)
		return NULL;
	snprintf(cfile, flen, "%s/%s", ctrl_iface_dir, ifname);

	ctrl_conn = wpa_ctrl_open(cfile);
	free(cfile);
	return ctrl_conn;
}


static void hostapd_cli_close_connection(void)
{
	if (ctrl_conn == NULL)
		return;

	if (hostapd_cli_attached) {
		wpa_ctrl_detach(ctrl_conn);
		hostapd_cli_attached = 0;
	}
	wpa_ctrl_close(ctrl_conn);
	ctrl_conn = NULL;
}


static void hostapd_cli_msg_cb(char *msg, size_t len)
{
	printf("%s\n", msg);
}


static int _wpa_ctrl_command(struct wpa_ctrl *ctrl, char *cmd, int print)
{
	char buf[4096];
	size_t len;
	int ret;

	if (ctrl_conn == NULL) {
		printf("Not connected to hostapd - command dropped.\n");
		return -1;
	}
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len,
			       hostapd_cli_msg_cb);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0) {
		printf("'%s' command failed.\n", cmd);
		return -1;
	}
	if (print) {
		buf[len] = '\0';
		printf("%s", buf);
	}
	return 0;
}


static inline int wpa_ctrl_command(struct wpa_ctrl *ctrl, char *cmd)
{
	return _wpa_ctrl_command(ctrl, cmd, 1);
}


static int hostapd_cli_cmd_ping(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "PING");
}


static int hostapd_cli_cmd_mib(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	return wpa_ctrl_command(ctrl, "MIB");
}


static int hostapd_cli_cmd_sta(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char buf[64];
	if (argc != 1) {
		printf("Invalid 'sta' command - exactly one argument, STA "
		       "address, is required.\n");
		return -1;
	}
	snprintf(buf, sizeof(buf), "STA %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}


static int hostapd_cli_cmd_new_sta(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	char buf[64];
	if (argc != 1) {
		printf("Invalid 'new_sta' command - exactly one argument, STA "
		       "address, is required.\n");
		return -1;
	}
	snprintf(buf, sizeof(buf), "NEW_STA %s", argv[0]);
	return wpa_ctrl_command(ctrl, buf);
}


#ifdef CONFIG_WPS
static int hostapd_cli_cmd_wps_pin(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	char buf[64];
	if (argc != 2) {
		printf("Invalid 'wps_pin' command - exactly two arguments, "
		       "UUID and PIN, are required.\n");
		return -1;
	}
	snprintf(buf, sizeof(buf), "WPS_PIN %s %s", argv[0], argv[1]);
	return wpa_ctrl_command(ctrl, buf);
}


static int hostapd_cli_cmd_wps_pbc(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	return wpa_ctrl_command(ctrl, "WPS_PBC");
}
#endif /* CONFIG_WPS */


static int wpa_ctrl_command_sta(struct wpa_ctrl *ctrl, char *cmd,
				char *addr, size_t addr_len)
{
	char buf[4096], *pos;
	size_t len;
	int ret;

	if (ctrl_conn == NULL) {
		printf("Not connected to hostapd - command dropped.\n");
		return -1;
	}
	len = sizeof(buf) - 1;
	ret = wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len,
			       hostapd_cli_msg_cb);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0) {
		printf("'%s' command failed.\n", cmd);
		return -1;
	}

	buf[len] = '\0';
	if (memcmp(buf, "FAIL", 4) == 0)
		return -1;
	printf("%s", buf);

	pos = buf;
	while (*pos != '\0' && *pos != '\n')
		pos++;
	*pos = '\0';
	os_strlcpy(addr, buf, addr_len);
	return 0;
}


static int hostapd_cli_cmd_all_sta(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	char addr[32], cmd[64];

	if (wpa_ctrl_command_sta(ctrl, "STA-FIRST", addr, sizeof(addr)))
		return 0;
	do {
		snprintf(cmd, sizeof(cmd), "STA-NEXT %s", addr);
	} while (wpa_ctrl_command_sta(ctrl, cmd, addr, sizeof(addr)) == 0);

	return -1;
}


static int hostapd_cli_cmd_help(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	printf("%s", commands_help);
	return 0;
}


static int hostapd_cli_cmd_license(struct wpa_ctrl *ctrl, int argc,
				   char *argv[])
{
	printf("%s\n\n%s\n", hostapd_cli_version, hostapd_cli_full_license);
	return 0;
}


static int hostapd_cli_cmd_quit(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	hostapd_cli_quit = 1;
	return 0;
}


static int hostapd_cli_cmd_level(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	char cmd[256];
	if (argc != 1) {
		printf("Invalid LEVEL command: needs one argument (debug "
		       "level)\n");
		return 0;
	}
	snprintf(cmd, sizeof(cmd), "LEVEL %s", argv[0]);
	return wpa_ctrl_command(ctrl, cmd);
}


static void hostapd_cli_list_interfaces(struct wpa_ctrl *ctrl)
{
	struct dirent *dent;
	DIR *dir;

	dir = opendir(ctrl_iface_dir);
	if (dir == NULL) {
		printf("Control interface directory '%s' could not be "
		       "openned.\n", ctrl_iface_dir);
		return;
	}

	printf("Available interfaces:\n");
	while ((dent = readdir(dir))) {
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0)
			continue;
		printf("%s\n", dent->d_name);
	}
	closedir(dir);
}


static int hostapd_cli_cmd_interface(struct wpa_ctrl *ctrl, int argc,
				     char *argv[])
{
	if (argc < 1) {
		hostapd_cli_list_interfaces(ctrl);
		return 0;
	}

	hostapd_cli_close_connection();
	free(ctrl_ifname);
	ctrl_ifname = strdup(argv[0]);

	if (hostapd_cli_open_connection(ctrl_ifname)) {
		printf("Connected to interface '%s.\n", ctrl_ifname);
		if (wpa_ctrl_attach(ctrl_conn) == 0) {
			hostapd_cli_attached = 1;
		} else {
			printf("Warning: Failed to attach to "
			       "hostapd.\n");
		}
	} else {
		printf("Could not connect to interface '%s' - re-trying\n",
			ctrl_ifname);
	}
	return 0;
}


struct hostapd_cli_cmd {
	const char *cmd;
	int (*handler)(struct wpa_ctrl *ctrl, int argc, char *argv[]);
};

static struct hostapd_cli_cmd hostapd_cli_commands[] = {
	{ "ping", hostapd_cli_cmd_ping },
	{ "mib", hostapd_cli_cmd_mib },
	{ "sta", hostapd_cli_cmd_sta },
	{ "all_sta", hostapd_cli_cmd_all_sta },
	{ "new_sta", hostapd_cli_cmd_new_sta },
#ifdef CONFIG_WPS
	{ "wps_pin", hostapd_cli_cmd_wps_pin },
	{ "wps_pbc", hostapd_cli_cmd_wps_pbc },
#endif /* CONFIG_WPS */
	{ "help", hostapd_cli_cmd_help },
	{ "interface", hostapd_cli_cmd_interface },
	{ "level", hostapd_cli_cmd_level },
	{ "license", hostapd_cli_cmd_license },
	{ "quit", hostapd_cli_cmd_quit },
	{ NULL, NULL }
};


static void wpa_request(struct wpa_ctrl *ctrl, int argc, char *argv[])
{
	struct hostapd_cli_cmd *cmd, *match = NULL;
	int count;

	count = 0;
	cmd = hostapd_cli_commands;
	while (cmd->cmd) {
		if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) == 0) {
			match = cmd;
			count++;
		}
		cmd++;
	}

	if (count > 1) {
		printf("Ambiguous command '%s'; possible commands:", argv[0]);
		cmd = hostapd_cli_commands;
		while (cmd->cmd) {
			if (strncasecmp(cmd->cmd, argv[0], strlen(argv[0])) ==
			    0) {
				printf(" %s", cmd->cmd);
			}
			cmd++;
		}
		printf("\n");
	} else if (count == 0) {
		printf("Unknown command '%s'\n", argv[0]);
	} else {
		match->handler(ctrl, argc - 1, &argv[1]);
	}
}


static void hostapd_cli_recv_pending(struct wpa_ctrl *ctrl, int in_read)
{
	int first = 1;
	if (ctrl_conn == NULL)
		return;
	while (wpa_ctrl_pending(ctrl)) {
		char buf[256];
		size_t len = sizeof(buf) - 1;
		if (wpa_ctrl_recv(ctrl, buf, &len) == 0) {
			buf[len] = '\0';
			if (in_read && first)
				printf("\n");
			first = 0;
			printf("%s\n", buf);
		} else {
			printf("Could not read pending message.\n");
			break;
		}
	}
}


static void hostapd_cli_interactive(void)
{
	const int max_args = 10;
	char cmd[256], *res, *argv[max_args], *pos;
	int argc;

	printf("\nInteractive mode\n\n");

	do {
		hostapd_cli_recv_pending(ctrl_conn, 0);
		printf("> ");
		alarm(1);
		res = fgets(cmd, sizeof(cmd), stdin);
		alarm(0);
		if (res == NULL)
			break;
		pos = cmd;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		argc = 0;
		pos = cmd;
		for (;;) {
			while (*pos == ' ')
				pos++;
			if (*pos == '\0')
				break;
			argv[argc] = pos;
			argc++;
			if (argc == max_args)
				break;
			while (*pos != '\0' && *pos != ' ')
				pos++;
			if (*pos == ' ')
				*pos++ = '\0';
		}
		if (argc)
			wpa_request(ctrl_conn, argc, argv);
	} while (!hostapd_cli_quit);
}


static void hostapd_cli_terminate(int sig)
{
	hostapd_cli_close_connection();
	exit(0);
}


static void hostapd_cli_alarm(int sig)
{
	if (ctrl_conn && _wpa_ctrl_command(ctrl_conn, "PING", 0)) {
		printf("Connection to hostapd lost - trying to reconnect\n");
		hostapd_cli_close_connection();
	}
	if (!ctrl_conn) {
		ctrl_conn = hostapd_cli_open_connection(ctrl_ifname);
		if (ctrl_conn) {
			printf("Connection to hostapd re-established\n");
			if (wpa_ctrl_attach(ctrl_conn) == 0) {
				hostapd_cli_attached = 1;
			} else {
				printf("Warning: Failed to attach to "
				       "hostapd.\n");
			}
		}
	}
	if (ctrl_conn)
		hostapd_cli_recv_pending(ctrl_conn, 1);
	alarm(1);
}


int main(int argc, char *argv[])
{
	int interactive;
	int warning_displayed = 0;
	int c;

	for (;;) {
		c = getopt(argc, argv, "hi:p:v");
		if (c < 0)
			break;
		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'v':
			printf("%s\n", hostapd_cli_version);
			return 0;
		case 'i':
			free(ctrl_ifname);
			ctrl_ifname = strdup(optarg);
			break;
		case 'p':
			ctrl_iface_dir = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	interactive = argc == optind;

	if (interactive) {
		printf("%s\n\n%s\n\n", hostapd_cli_version,
		       hostapd_cli_license);
	}

	for (;;) {
		if (ctrl_ifname == NULL) {
			struct dirent *dent;
			DIR *dir = opendir(ctrl_iface_dir);
			if (dir) {
				while ((dent = readdir(dir))) {
					if (strcmp(dent->d_name, ".") == 0 ||
					    strcmp(dent->d_name, "..") == 0)
						continue;
					printf("Selected interface '%s'\n",
					       dent->d_name);
					ctrl_ifname = strdup(dent->d_name);
					break;
				}
				closedir(dir);
			}
		}
		ctrl_conn = hostapd_cli_open_connection(ctrl_ifname);
		if (ctrl_conn) {
			if (warning_displayed)
				printf("Connection established.\n");
			break;
		}

		if (!interactive) {
			perror("Failed to connect to hostapd - "
			       "wpa_ctrl_open");
			return -1;
		}

		if (!warning_displayed) {
			printf("Could not connect to hostapd - re-trying\n");
			warning_displayed = 1;
		}
		sleep(1);
		continue;
	}

	signal(SIGINT, hostapd_cli_terminate);
	signal(SIGTERM, hostapd_cli_terminate);
	signal(SIGALRM, hostapd_cli_alarm);

	if (interactive) {
		if (wpa_ctrl_attach(ctrl_conn) == 0) {
			hostapd_cli_attached = 1;
		} else {
			printf("Warning: Failed to attach to hostapd.\n");
		}
		hostapd_cli_interactive();
	} else
		wpa_request(ctrl_conn, argc - optind, &argv[optind]);

	free(ctrl_ifname);
	hostapd_cli_close_connection();
	return 0;
}
