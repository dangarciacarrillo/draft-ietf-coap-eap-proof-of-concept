/*
 * sysutmp.h	Compatibility stuff for the different UTMP systems.
 *
 * Version:	$Id: sysutmp.h,v 1.11 2006/11/14 21:21:46 fcusack Exp $
 */

#ifndef SYSUTMP_H_INCLUDED
#define SYSUTMP_H_INCLUDED

#include <freeradius-devel/ident.h>
RCSIDH(sysutmp_h, "$Id: sysutmp.h,v 1.11 2006/11/14 21:21:46 fcusack Exp $")

/*
 *  If we have BOTH utmp.h and utmpx.h, then
 *  we prefer to use utmp.h, but only on systems other than Solaris.
 */
#if !defined(sun) && !defined(sgi) && !defined(hpux)
#ifdef HAVE_UTMP_H
#undef HAVE_UTMPX_H
#endif
#endif

#if defined(HAVE_UTMP_H) || defined(HAVE_UTMPX_H)

/* UTMP stuff. Uses utmpx on svr4 */
#ifdef HAVE_UTMPX_H
#  include <utmpx.h>
#  include <sys/fcntl.h>
#  define utmp utmpx
#  define UT_NAMESIZE	32
#  define UT_LINESIZE	32
#  define UT_HOSTSIZE	257
#ifdef hpux
#  define ut_name ut_user
#endif
#else
#  include <utmp.h>
#endif

#ifdef __osf__
#  define UT_NAMESIZE	32
#  define UT_LINESIZE	32
#  define UT_HOSTSIZE	64
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(bsdi) || defined(__OpenBSD__) || defined(__APPLE__)
#  ifndef UTMP_FILE
#    define UTMP_FILE "/var/run/utmp"
#  endif
#  define ut_user ut_name
#endif

/*
 *	Generate definitions for systems which are too broken to
 *	do it themselves.
 *
 *	Hmm... this means that we can probably get rid of a lot of
 *	the static defines above, as the following lines will generate
 *	the proper defines for any system.
 */
#ifndef UT_LINESIZE
#define UT_LINESIZE sizeof(((struct utmp *) NULL)->ut_line)
#endif

#ifndef UT_NAMESIZE
#define UT_NAMESIZE sizeof(((struct utmp *) NULL)->ut_user)
#endif

#ifndef UT_HOSTSIZE
#define UT_HOSTSIZE sizeof(((struct utmp *) NULL)->ut_host)
#endif

#else /* HAVE_UTMP_H */

/*
 *	No <utmp.h> file - define stuff ourselves (minimally).
 */
#define UT_LINESIZE	16
#define UT_NAMESIZE	16
#define UT_HOSTSIZE	16

#define USER_PROCESS	7
#define DEAD_PROCESS	8

#define UTMP_FILE	"/var/run/utmp"
#define ut_name		ut_user

struct utmp {
	short	ut_type;
	int	ut_pid;
	char	ut_line[UT_LINESIZE];
	char	ut_id[4];
	long	ut_time;
	char	ut_user[UT_NAMESIZE];
	char	ut_host[UT_HOSTSIZE];
	long	ut_addr;
};

#endif /* HAVE_UTMP_H */

#endif /* SYSUTMP_H_INCLUDED */
