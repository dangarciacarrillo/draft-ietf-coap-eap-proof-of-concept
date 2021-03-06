/*
 * mainconf.c	Handle the server's configuration.
 *
 * Version:	$Id: mainconfig.c,v 1.145 2008/01/21 10:29:02 aland Exp $
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2002,2006-2007  The FreeRADIUS server project
 * Copyright 2002  Alan DeKok <aland@ox.org>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: mainconfig.c,v 1.145 2008/01/21 10:29:02 aland Exp $")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <sys/stat.h>

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifdef HAVE_SYSLOG_H
#	include <syslog.h>
#endif

struct main_config_t mainconfig;

/*
 *	Temporary local variables for parsing the configuration
 *	file.
 */
static uid_t server_uid;
static gid_t server_gid;
static const char *uid_name = NULL;
static const char *gid_name = NULL;
static int allow_core_dumps = 0;
static const char *radlog_dest = NULL;

/*
 *	These are not used anywhere else..
 */
static const char *localstatedir = NULL;
static const char *prefix = NULL;
static char *syslog_facility = NULL;
static const FR_NAME_NUMBER str2fac[] = {
#ifdef LOG_KERN
	{ "kern", LOG_KERN },
#endif
#ifdef LOG_USER
	{ "user", LOG_USER },
#endif
#ifdef LOG_MAIL
	{ "mail", LOG_MAIL },
#endif
#ifdef LOG_DAEMON
	{ "daemon", LOG_DAEMON },
#endif
#ifdef LOG_AUTH
	{ "auth", LOG_AUTH },
#endif
#ifdef LOG_LPR
	{ "lpr", LOG_LPR },
#endif
#ifdef LOG_NEWS
	{ "news", LOG_NEWS },
#endif
#ifdef LOG_UUCP
	{ "uucp", LOG_UUCP },
#endif
#ifdef LOG_CRON
	{ "cron", LOG_CRON },
#endif
#ifdef LOG_AUTHPRIV
	{ "authpriv", LOG_AUTHPRIV },
#endif
#ifdef LOG_FTP
	{ "ftp", LOG_FTP },
#endif
#ifdef LOG_LOCAL0
	{ "local0", LOG_LOCAL0 },
#endif
#ifdef LOG_LOCAL1
	{ "local1", LOG_LOCAL1 },
#endif
#ifdef LOG_LOCAL2
	{ "local2", LOG_LOCAL2 },
#endif
#ifdef LOG_LOCAL3
	{ "local3", LOG_LOCAL3 },
#endif
#ifdef LOG_LOCAL4
	{ "local4", LOG_LOCAL4 },
#endif
#ifdef LOG_LOCAL5
	{ "local5", LOG_LOCAL5 },
#endif
#ifdef LOG_LOCAL6
	{ "local6", LOG_LOCAL6 },
#endif
#ifdef LOG_LOCAL7
	{ "local7", LOG_LOCAL7 },
#endif
	{ NULL, -1 }
};

/*
 *  Security configuration for the server.
 */
static const CONF_PARSER security_config[] = {
	{ "max_attributes",  PW_TYPE_INTEGER, 0, &librad_max_attributes, Stringify(0) },
	{ "reject_delay",  PW_TYPE_INTEGER, 0, &mainconfig.reject_delay, Stringify(0) },
	{ "status_server", PW_TYPE_BOOLEAN, 0, &mainconfig.status_server, "no"},
	{ NULL, -1, 0, NULL, NULL }
};


/*
 *	Logging configuration for the server.
 */
static const CONF_PARSER log_config[] = {
	{ "destination",  PW_TYPE_STRING_PTR, 0, &radlog_dest, "files" },
	{ "syslog_facility",  PW_TYPE_STRING_PTR, 0, &syslog_facility, Stringify(0) },
	{ "stripped_names", PW_TYPE_BOOLEAN, 0, &log_stripped_names,"no" },

	{ "file", PW_TYPE_STRING_PTR, -1, &mainconfig.log_file, "${logdir}/radius.log" },
	{ "auth", PW_TYPE_BOOLEAN, -1, &mainconfig.log_auth, "no" },
	{ "auth_badpass", PW_TYPE_BOOLEAN, 0, &mainconfig.log_auth_badpass, "no" },
	{ "auth_goodpass", PW_TYPE_BOOLEAN, 0, &mainconfig.log_auth_goodpass, "no" },
	{ NULL, -1, 0, NULL, NULL }
};


/*
 *  A mapping of configuration file names to internal variables
 */
static const CONF_PARSER server_config[] = {
	/*
	 *	FIXME: 'prefix' is the ONLY one which should be
	 *	configured at compile time.  Hard-coding it here is
	 *	bad.  It will be cleaned up once we clean up the
	 *	hard-coded defines for the locations of the various
	 *	files.
	 */
	{ "prefix",             PW_TYPE_STRING_PTR, 0, &prefix,            "/usr/local"},
	{ "localstatedir",      PW_TYPE_STRING_PTR, 0, &localstatedir,     "${prefix}/var"},
	{ "logdir",             PW_TYPE_STRING_PTR, 0, &radlog_dir,        "${localstatedir}/log"},
	{ "libdir",             PW_TYPE_STRING_PTR, 0, &radlib_dir,        "${prefix}/lib"},
	{ "radacctdir",         PW_TYPE_STRING_PTR, 0, &radacct_dir,       "${logdir}/radacct" },
	{ "hostname_lookups",   PW_TYPE_BOOLEAN,    0, &librad_dodns,      "no" },
	{ "max_request_time", PW_TYPE_INTEGER, 0, &mainconfig.max_request_time, Stringify(MAX_REQUEST_TIME) },
	{ "cleanup_delay", PW_TYPE_INTEGER, 0, &mainconfig.cleanup_delay, Stringify(CLEANUP_DELAY) },
	{ "max_requests", PW_TYPE_INTEGER, 0, &mainconfig.max_requests, Stringify(MAX_REQUESTS) },
#ifdef DELETE_BLOCKED_REQUESTS
	{ "delete_blocked_requests", PW_TYPE_INTEGER, 0, &mainconfig.kill_unresponsive_children, Stringify(FALSE) },
#endif
	{ "allow_core_dumps", PW_TYPE_BOOLEAN, 0, &allow_core_dumps, "no" },

	{ "pidfile", PW_TYPE_STRING_PTR, 0, &mainconfig.pid_file, "${run_dir}/radiusd.pid"},
	{ "user", PW_TYPE_STRING_PTR, 0, &uid_name, NULL},
	{ "group", PW_TYPE_STRING_PTR, 0, &gid_name, NULL},
	{ "checkrad", PW_TYPE_STRING_PTR, 0, &mainconfig.checkrad, "${sbindir}/checkrad" },

	{ "debug_level", PW_TYPE_INTEGER, 0, &mainconfig.debug_level, "0"},

	{ "proxy_requests", PW_TYPE_BOOLEAN, 0, &mainconfig.proxy_requests, "yes" },
	{ "security", PW_TYPE_SUBSECTION, 0, NULL, (const void *) security_config },
	{ NULL, -1, 0, NULL, NULL }
};

static const CONF_PARSER serverlog_config[] = {
	{ "log", PW_TYPE_SUBSECTION, 0, NULL,  (const void *) log_config},

	/*
	 *	People with old configs will have these.  They are listed
	 *	AFTER the "log" section, so if they exist in radiusd.conf,
	 *	it will prefer "log_foo = bar" to "log { foo = bar }".
	 *	They're listed with default values of NULL, so that if they
	 *	DON'T exist in radiusd.conf, then the previously parsed
	 *	values for "log { foo = bar}" will be used.
	 */
	{ "log_auth", PW_TYPE_BOOLEAN, -1, &mainconfig.log_auth, NULL },
	{ "log_auth_badpass", PW_TYPE_BOOLEAN, 0, &mainconfig.log_auth_badpass, NULL },
	{ "log_auth_goodpass", PW_TYPE_BOOLEAN, 0, &mainconfig.log_auth_goodpass, NULL },
	{ "log_stripped_names", PW_TYPE_BOOLEAN, 0, &log_stripped_names, NULL },
	{ "log_file", PW_TYPE_STRING_PTR, -1, &mainconfig.log_file, NULL },
	{ "log_destination", PW_TYPE_STRING_PTR, -1, &radlog_dest, "files" },
	{ NULL, -1, 0, NULL, NULL }
};


#define MAX_ARGV (256)


static size_t config_escape_func(char *out, size_t outlen, const char *in)
{
	size_t len = 0;
	static const char *disallowed = "%{}\\'\"`";

	while (in[0]) {
		/*
		 *	Non-printable characters get replaced with their
		 *	mime-encoded equivalents.
		 */
		if ((in[0] < 32)) {
			if (outlen <= 3) break;

			snprintf(out, outlen, "=%02X", (unsigned char) in[0]);
			in++;
			out += 3;
			outlen -= 3;
			len += 3;
			continue;

		} else if (strchr(disallowed, *in) != NULL) {
			if (outlen <= 2) break;

			out[0] = '\\';
			out[1] = *in;
			in++;
			out += 2;
			outlen -= 2;
			len += 2;
			continue;
		}

		/*
		 *	Only one byte left.
		 */
		if (outlen <= 1) {
			break;
		}

		/*
		 *	Allowed character.
		 */
		*out = *in;
		out++;
		in++;
		outlen--;
		len++;
	}
	*out = '\0';
	return len;
}

/*
 *	Xlat for %{config:section.subsection.attribute}
 */
static size_t xlat_config(void *instance, REQUEST *request,
			  char *fmt, char *out,
			  size_t outlen,
			  RADIUS_ESCAPE_STRING func)
{
	const char *value;
	CONF_PAIR *cp;
	CONF_ITEM *ci;
	char buffer[1024];

	request = request;	/* -Wunused */
	instance = instance;	/* -Wunused */

	/*
	 *	Expand it safely.
	 */
	if (!radius_xlat(buffer, sizeof(buffer), fmt, request, config_escape_func)) {
		return 0;
	}

	ci = cf_reference_item(request->root->config,
			       request->root->config, buffer);
	if (!ci || !cf_item_is_pair(ci)) {
		*out = '\0';
		return 0;
	}

	cp = cf_itemtopair(ci);

	/*
	 *  Ensure that we only copy what's necessary.
	 *
	 *  If 'outlen' is too small, then the output is chopped to fit.
	 */
	value = cf_pair_value(cp);
	if (value) {
		if (outlen > strlen(value)) {
			outlen = strlen(value) + 1;
		}
	}

	return func(out, outlen, value);
}


/*
 *	Xlat for %{client:foo}
 */
static size_t xlat_client(UNUSED void *instance, REQUEST *request,
		       char *fmt, char *out,
		       size_t outlen,
		       UNUSED RADIUS_ESCAPE_STRING func)
{
	const char *value = NULL;
	CONF_PAIR *cp;

	if (!fmt || !out || (outlen < 1)) return 0;

	if (!request || !request->client) {
		*out = '\0';
		return 0;
	}

	cp = cf_pair_find(request->client->cs, fmt);
	if (!cp || !(value = cf_pair_value(cp))) {
		*out = '\0';
		return 0;
	}
	
	strlcpy(out, value, outlen);

	return strlen(out);
}

/*
 *	Recursively make directories.
 */
static int r_mkdir(const char *part)
{
	char *ptr, parentdir[500];
	struct stat st;

	if (stat(part, &st) == 0)
		return(0);

	ptr = strrchr(part, FR_DIR_SEP);

	if (ptr == part)
		return(0);

	snprintf(parentdir, (ptr - part)+1, "%s", part);

	if (r_mkdir(parentdir) != 0)
		return(1);

	if (mkdir(part, 0770) != 0) {
		radlog(L_ERR, "mkdir(%s) error: %s\n", part, strerror(errno));
		return(1);
	}

	return(0);
}

/*
 *	Checks if the log directory is writeable by a particular user.
 */
static int radlogdir_iswritable(const char *effectiveuser)
{
#ifdef HAVE_GETPWNAM
	struct passwd *pwent;
#endif

	if (!radlog_dir || FR_DIR_IS_RELATIVE(radlog_dir))
		return(0);

	if (r_mkdir(radlog_dir) != 0)
		return(1);

	/* FIXME: do we have this function? */
	if (strstr(radlog_dir, "radius") == NULL)
		return(0);

	/* we have a logdir that mentions 'radius', so it's probably
	 * safe to chown the immediate directory to be owned by the normal
	 * process owner. we gotta do it before we give up root.  -chad
	 */

	if (!effectiveuser) {
		return 1;
	}

#ifdef HAVE_GETPWNAM
	pwent = getpwnam(effectiveuser);

	if (pwent == NULL) /* uh oh! */
		return(1);

	if (chown(radlog_dir, pwent->pw_uid, -1) != 0)
		return(1);
#endif

	return(0);
}


/*
 *  Switch UID and GID to what is specified in the config file
 */
static int switch_users(void)
{
	int did_setuid = FALSE;

#ifdef HAVE_SYS_RESOURCE_H
	struct rlimit core_limits;
#endif

#ifdef HAVE_GRP_H
	/*  Set GID.  */
	if (gid_name != NULL) {
		struct group *gr;

		gr = getgrnam(gid_name);
		if (gr == NULL) {
			if (errno == ENOMEM) {
				radlog(L_ERR, "Cannot switch to Group %s: out of memory", gid_name);
			} else {
				radlog(L_ERR, "Cannot switch group; %s doesn't exist", gid_name);
			}
			return 0;
		}
		server_gid = gr->gr_gid;
		if (setgid(server_gid) < 0) {
			radlog(L_ERR, "Failed setting Group to %s: %s",
			       gid_name, strerror(errno));
			return 0;
		}
	} else {
		server_gid = getgid();
	}
#endif

#ifdef HAVE_PWD_H
	/*  Set UID.  */
	if (uid_name != NULL) {
		struct passwd *pw;

		pw = getpwnam(uid_name);
		if (pw == NULL) {
			if (errno == ENOMEM) {
				radlog(L_ERR, "Cannot switch to User %s: out of memory", uid_name);
			} else {
				radlog(L_ERR, "Cannot switch user; %s doesn't exist", uid_name);
			}
			return 0;
		}
		server_uid = pw->pw_uid;
#ifdef HAVE_INITGROUPS
		if (initgroups(uid_name, server_gid) < 0) {
			if (errno != EPERM) {
				radlog(L_ERR, "Failed setting supplementary groups for User %s: %s", uid_name, strerror(errno));
				return 0;
			}
		}
#endif
		if (setuid(server_uid) < 0) {
			radlog(L_ERR, "Failed setting User to %s: %s", uid_name, strerror(errno));
			return 0;
		}

		/*
		 *	Now core dumps are disabled on most secure systems.
		 */
		did_setuid = TRUE;
	}
#endif

#ifdef HAVE_SYS_RESOURCE_H
	/*  Get the current maximum for core files.  */
	if (getrlimit(RLIMIT_CORE, &core_limits) < 0) {
		radlog(L_ERR, "Failed to get current core limit:  %s", strerror(errno));
		return 0;
	}
#endif

	/*
	 *	Core dumps are allowed if we're in debug mode, OR
	 *	we've allowed them, OR we did a setuid (which turns
	 *	core dumps off).
	 *
	 *	Otherwise, disable core dumps for security.
	 *	
	 */
	if (!(debug_flag || allow_core_dumps || did_setuid)) {
#ifdef HAVE_SYS_RESOURCE_H
		struct rlimit no_core;

		no_core.rlim_cur = 0;
		no_core.rlim_max = 0;

		if (setrlimit(RLIMIT_CORE, &no_core) < 0) {
			radlog(L_ERR, "Failed disabling core dumps: %s",
			       strerror(errno));
			return 0;
		}
#endif

		/*
		 *	Otherwise, re-enable core dumps if we're
		 *	running as a daemon, AND core dumps are
		 *	allowed, AND we changed UID's.
		 */
	} else if ((debug_flag == 0) && allow_core_dumps && did_setuid) {
		/*
		 *	Set the dumpable flag.
		 */
#ifdef HAVE_SYS_PRCTL_H
#ifdef PR_SET_DUMPABLE
		if (prctl(PR_SET_DUMPABLE, 1) < 0) {
			radlog(L_ERR,"Cannot enable core dumps: prctl(PR_SET_DUMPABLE) failed: '%s'",
			       strerror(errno));
		}
#endif
#endif

		/*
		 *	Reset the core dump limits again, just to
		 *	double check that they haven't changed.
		 */
#ifdef HAVE_SYS_RESOURCE_H
		if (setrlimit(RLIMIT_CORE, &core_limits) < 0) {
			radlog(L_ERR, "Cannot update core dump limit: %s",
					strerror(errno));
			return 0;
		}
#endif

		radlog(L_INFO, "Core dumps are enabled.");
	}
	/*
	 *	Else we're debugging (so core dumps are enabled)
	 *	OR we're not debugging, AND "allow_core_dumps == FALSE",
	 *	OR we're not debugging, AND core dumps are allowed,
	 *	   BUT we didn't call setuid, so we haven't changed the
	 *	   core dump capabilities inherited from the parent shell.
	 */

#if defined(HAVE_PWD_H) && defined(HAVE_GRP_H)
	/*
	 *	We've probably written to the log file already as
	 *	root.root, so if we have switched users, we've got to
	 *	update the ownership of the file.
	 */
	if ((debug_flag == 0) &&
	    (mainconfig.radlog_dest == RADLOG_FILES) &&
	    (mainconfig.log_file != NULL)) {
		chown(mainconfig.log_file, server_uid, server_gid);
	}
#endif
	return 1;
}


static const FR_NAME_NUMBER str2dest[] = {
	{ "null", RADLOG_NULL },
	{ "files", RADLOG_FILES },
	{ "syslog", RADLOG_SYSLOG },
	{ "stdout", RADLOG_STDOUT },
	{ "stderr", RADLOG_STDERR },
	{ NULL, RADLOG_NUM_DEST }
};


/*
 *	Read config files.
 *
 *	This function can ONLY be called from the main server process.
 */
int read_mainconfig(int reload)
{
	const char *p = NULL;
	static int old_debug_level = -1;
	CONF_PAIR *cp;
	CONF_SECTION *cs;
	struct stat statbuf;
	char buffer[1024];

	if (stat(radius_dir, &statbuf) < 0) {
		radlog(L_ERR, "Errors reading %s: %s",
		       radius_dir, strerror(errno));
		return -1;
	}

#ifdef S_IWOTH
	if ((statbuf.st_mode & S_IWOTH) != 0) {
		radlog(L_ERR, "Configuration directory %s is globally writable.  Refusing to start due to insecure configuration.",
		       radius_dir);
	  return -1;
	}
#endif

#ifdef S_IROTH
	if (0 && (statbuf.st_mode & S_IROTH) != 0) {
		radlog(L_ERR, "Configuration directory %s is globally readable.  Refusing to start due to insecure configuration.",
		       radius_dir);
		return -1;
	}
#endif

	if (!reload) {
		radlog(L_INFO, "Starting - reading configuration files ...");
	} else {
		radlog(L_INFO, "Reloading - reading configuration files...");
	}

	/* Read the configuration file */
	snprintf(buffer, sizeof(buffer), "%.200s/%.50s.conf",
		 radius_dir, mainconfig.name);
	if ((cs = cf_file_read(buffer)) == NULL) {
		radlog(L_ERR, "Errors reading %s", buffer);
		return -1;
	}

	/*
	 *	Debug flag 1 MAY go to files.
	 *	Debug flag 2 ALWAYS goes to stdout
	 *
	 *	Parse the log{} section before printing anything else.
	 *	All messages before this MUST be errors, which log.c
	 *	will print to stderr.
	 */
	if (debug_flag < 2) {
		if (cf_section_parse(cs, NULL, serverlog_config) < 0) {
			fprintf(stderr, "radiusd: Error: Failed to parse log{} section.\n");
			cf_section_free(&cs);
			return -1;
		}

		if (!radlog_dest) {
			fprintf(stderr, "radiusd: Error: No log destination specified.\n");
			cf_section_free(&cs);
			return -1;
		}

		mainconfig.radlog_dest = fr_str2int(str2dest, radlog_dest,
						    RADLOG_NUM_DEST);
		if (mainconfig.radlog_dest == RADLOG_NUM_DEST) {
			fprintf(stderr, "radiusd: Error: Unknown log_destination %s\n",
				radlog_dest);
			cf_section_free(&cs);
			return -1;
		}

		if (mainconfig.radlog_dest == RADLOG_SYSLOG) {
			/*
			 *	Make sure syslog_facility isn't NULL
			 *	before using it
			 */
			if (!syslog_facility) {
				fprintf(stderr, "radiusd: Error: Syslog chosen but no facility was specified\n");
				cf_section_free(&cs);
				return -1;
			}
			mainconfig.syslog_facility = fr_str2int(str2fac, syslog_facility, -1);
			if (mainconfig.syslog_facility < 0) {
				fprintf(stderr, "radiusd: Error: Unknown syslog_facility %s\n",
					syslog_facility);
				cf_section_free(&cs);
				return -1;
			}
		}
	} else {
		mainconfig.radlog_dest = RADLOG_STDOUT;
		mainconfig.radlog_fd = STDOUT_FILENO;
	}

	/* Initialize the dictionary */
	cp = cf_pair_find(cs, "dictionary");
	if (cp) p = cf_pair_value(cp);
	if (!p) p = radius_dir;
	DEBUG2("including dictionary file %s/%s", p, RADIUS_DICTIONARY);
	if (dict_init(p, RADIUS_DICTIONARY) != 0) {
		radlog(L_ERR, "Errors reading dictionary: %s",
				librad_errstr);
		return -1;
	}

	/*
	 *	This allows us to figure out where, relative to
	 *	radiusd.conf, the other configuration files exist.
	 */
	cf_section_parse(cs, NULL, server_config);

	/*
	 *	Free the old configuration items, and replace them
	 *	with the new ones.
	 *
	 *	Note that where possible, we do atomic switch-overs,
	 *	to ensure that the pointers are always valid.
	 */
	cf_section_free(&mainconfig.config);
	mainconfig.config = cs;

	clients_parse_section(cs);

	DEBUG2("%s: #### Loading Realms and Home Servers ####", mainconfig.name);

	if (!realms_init(cs)) {
		return -1;
	}

	/*
	 *  Register the %{config:section.subsection} xlat function.
	 */
	xlat_register("config", xlat_config, NULL);
	xlat_register("client", xlat_client, NULL);

	/*
	 *	Reload: change debug flag if it's changed in the
	 *	configuration file.
	 */
	if (reload) {
		if (mainconfig.debug_level != old_debug_level) {
			debug_flag = mainconfig.debug_level;
		}

	} else if (debug_flag == 0) {

		/*
		 *	Starting the server, WITHOUT "-x" on the
		 *	command-line: use whatever's in the config
		 *	file.
		 */
		debug_flag = mainconfig.debug_level;
	}
	librad_debug = debug_flag;
	old_debug_level = mainconfig.debug_level;

	/*
	 *  Go update our behaviour, based on the configuration
	 *  changes.
	 */

	/*
	 * 	The first time around, ensure that we can write to the
	 *	log directory.
	 */
	if (!reload && !debug_flag) {
		/*
		 *	We need root to do mkdir() and chown(), so we
		 *	do this before giving up root.
		 */
		radlogdir_iswritable(uid_name);
	}

	/*
	 *	We should really switch users earlier in the process.
	 */
	if (!switch_users()) exit(1);

	/*
	 *	Sanity check the configuration for internal
	 *	consistency.
	 */
	if (mainconfig.reject_delay > mainconfig.cleanup_delay) {
		mainconfig.reject_delay = mainconfig.cleanup_delay;
	}
	if (mainconfig.reject_delay < 0) mainconfig.reject_delay = 0;

	/*  Reload the modules.  */
	if (setup_modules(reload, mainconfig.config) < 0) {
		radlog(L_ERR, "Errors initializing modules");
		return -1;
	}

	return 0;
}

/*
 *	Free the configuration.  Called only when the server is exiting.
 */
int free_mainconfig(void)
{
	/*
	 *	Clean up the configuration data
	 *	structures.
	 */
	cf_section_free(&mainconfig.config);
	realms_free();
	listen_free(&mainconfig.listen);
	dict_free();

	return 0;
}
