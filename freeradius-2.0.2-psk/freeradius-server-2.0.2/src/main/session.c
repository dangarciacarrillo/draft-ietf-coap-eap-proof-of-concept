/*
 * session.c	session management
 *
 * Version:	$Id: session.c,v 1.43 2007/11/23 13:46:52 aland Exp $
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
 * Copyright 2000,2006  The FreeRADIUS server project
 */

#include	<freeradius-devel/ident.h>
RCSID("$Id: session.c,v 1.43 2007/11/23 13:46:52 aland Exp $")

#include	<freeradius-devel/radiusd.h>
#include	<freeradius-devel/rad_assert.h>

#ifdef HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif

/*
 *	End a session by faking a Stop packet to all accounting modules.
 */
int session_zap(REQUEST *request, uint32_t nasaddr, unsigned int port,
		const char *user,
		const char *sessionid, uint32_t cliaddr, char proto,
		int session_time)
{
	REQUEST *stopreq;
	VALUE_PAIR *vp, *userpair;
	int ret;

	stopreq = request_alloc_fake(request);
	stopreq->packet->code = PW_ACCOUNTING_REQUEST; /* just to be safe */
	stopreq->listener = request->listener;
	rad_assert(stopreq != NULL);

	/* Hold your breath */
#define PAIR(n,v,t,e) do { \
		if(!(vp = paircreate(n, t))) { \
			request_free(&stopreq); \
			radlog(L_ERR|L_CONS, "no memory"); \
			pairfree(&(stopreq->packet->vps)); \
			return 0; \
		} \
		vp->e = v; \
		pairadd(&(stopreq->packet->vps), vp); \
	} while(0)
#define INTPAIR(n,v) PAIR(n,v,PW_TYPE_INTEGER,vp_integer)
#define IPPAIR(n,v) PAIR(n,v,PW_TYPE_IPADDR,vp_ipaddr)
#define STRINGPAIR(n,v) do { \
	if(!(vp = paircreate(n, PW_TYPE_STRING))) { \
		request_free(&stopreq); \
		radlog(L_ERR|L_CONS, "no memory"); \
		pairfree(&(stopreq->packet->vps)); \
		return 0; \
	} \
	strlcpy((char *)vp->vp_strvalue, v, sizeof vp->vp_strvalue); \
	vp->length = strlen(v); \
	pairadd(&(stopreq->packet->vps), vp); \
	} while(0)

	INTPAIR(PW_ACCT_STATUS_TYPE, PW_STATUS_STOP);
	IPPAIR(PW_NAS_IP_ADDRESS, nasaddr);
	INTPAIR(PW_ACCT_DELAY_TIME, 0);
	STRINGPAIR(PW_USER_NAME, user);
	userpair = vp;
	INTPAIR(PW_NAS_PORT, port);
	STRINGPAIR(PW_ACCT_SESSION_ID, sessionid);
	if(proto == 'P') {
		INTPAIR(PW_SERVICE_TYPE, PW_FRAMED_USER);
		INTPAIR(PW_FRAMED_PROTOCOL, PW_PPP);
	} else if(proto == 'S') {
		INTPAIR(PW_SERVICE_TYPE, PW_FRAMED_USER);
		INTPAIR(PW_FRAMED_PROTOCOL, PW_SLIP);
	} else {
		INTPAIR(PW_SERVICE_TYPE, PW_LOGIN_USER); /* A guess, really */
	}
	if(cliaddr != 0)
		IPPAIR(PW_FRAMED_IP_ADDRESS, cliaddr);
	INTPAIR(PW_ACCT_SESSION_TIME, session_time);
	INTPAIR(PW_ACCT_INPUT_OCTETS, 0);
	INTPAIR(PW_ACCT_OUTPUT_OCTETS, 0);
	INTPAIR(PW_ACCT_INPUT_PACKETS, 0);
	INTPAIR(PW_ACCT_OUTPUT_PACKETS, 0);

	stopreq->username = userpair;
	stopreq->password = NULL;

	ret = rad_accounting(stopreq);

	/*
	 *  We've got to clean it up by hand, because no one else will.
	 */
	request_free(&stopreq);

	return ret;
}

#ifndef __MINGW32__

/*
 *	Check one terminal server to see if a user is logged in.
 *
 *	Return values:
 *		0 The user is off-line.
 *		1 The user is logged in.
 *		2 Some error occured.
 */
int rad_check_ts(uint32_t nasaddr, unsigned int portnum, const char *user,
		 const char *session_id)
{
	pid_t	pid, child_pid;
	int	status;
	char	address[16];
	char	port[11];
	RADCLIENT *cl;
	fr_ipaddr_t ipaddr;

	ipaddr.af = AF_INET;
	ipaddr.ipaddr.ip4addr.s_addr = nasaddr;

	/*
	 *	Find NAS type.
	 */
	cl = client_find_old(&ipaddr);
	if (!cl) {
		/*
		 *  Unknown NAS, so trusting radutmp.
		 */
		DEBUG2("checkrad: Unknown NAS %s, not checking",
		       ip_ntoa(address, nasaddr));
		return 1;
	}

	/*
	 *  No nastype, or nas type 'other', trust radutmp.
	 */
	if (!cl->nastype || (cl->nastype[0] == '\0') ||
	    (strcmp(cl->nastype, "other") == 0)) {
		DEBUG2("checkrad: No NAS type, or type \"other\" not checking");
		return 1;
	}

	/*
	 *	Fork.
	 */
	if ((pid = rad_fork()) < 0) { /* do wait for the fork'd result */
		radlog(L_ERR, "Accounting: Failed in fork(): Cannot run checkrad\n");
		return 2;
	}

	if (pid > 0) {
		child_pid = rad_waitpid(pid, &status);

		/*
		 *	It's taking too long.  Stop waiting for it.
		 *
		 *	Don't bother to kill it, as we don't care what
		 *	happens to it now.
		 */
		if (child_pid == 0) {
			radlog(L_ERR, "Check-TS: timeout waiting for checkrad");
			return 2;
		}

		if (child_pid < 0) {
			radlog(L_ERR, "Check-TS: unknown error in waitpid()");
			return 2;
		}

		return WEXITSTATUS(status);
	}

	/*
	 *  We don't close fd's 0, 1, and 2.  If we're in debugging mode,
	 *  then they should go to stdout (etc), along with the other
	 *  server log messages.
	 *
	 *  If we're not in debugging mode, then the code in radiusd.c
	 *  takes care of connecting fd's 0, 1, and 2 to /dev/null.
	 */
	closefrom(3);

	ip_ntoa(address, nasaddr);
	snprintf(port, 11, "%u", portnum);

#ifdef __EMX__
	/* OS/2 can't directly execute scripts then we call the command
	   processor to execute checkrad
	*/
	execl(getenv("COMSPEC"), "", "/C","checkrad", cl->nastype, address, port,
		user, session_id, NULL);
#else
	execl(mainconfig.checkrad, "checkrad", cl->nastype, address, port,
		user, session_id, NULL);
#endif
	radlog(L_ERR, "Check-TS: exec %s: %s", mainconfig.checkrad, strerror(errno));

	/*
	 *	Exit - 2 means "some error occured".
	 */
	exit(2);
	return 2;
}
#else
int rad_check_ts(UNUSED uint32_t nasaddr, UNUSED unsigned int portnum,
		 UNUSED const char *user, UNUSED const char *session_id)
{
	radlog(L_ERR, "Simultaneous-Use is not supported");
	return 2;
}
#endif
