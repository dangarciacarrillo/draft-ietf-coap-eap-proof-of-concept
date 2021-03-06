/*
 * misc.c	Various miscellaneous functions.
 *
 * Version:	$Id: misc.c,v 1.82 2008/01/10 10:13:04 aland Exp $
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2000,2006  The FreeRADIUS server project
 */

#include	<freeradius-devel/ident.h>
RCSID("$Id: misc.c,v 1.82 2008/01/10 10:13:04 aland Exp $")

#include	<freeradius-devel/libradius.h>

#include	<ctype.h>
#include	<sys/file.h>
#include	<fcntl.h>

int		librad_dodns = 0;
int		librad_debug = 0;


/*
 *	Return an IP address in standard dot notation
 *
 *	FIXME: DELETE THIS
 */
const char *ip_ntoa(char *buffer, uint32_t ipaddr)
{
	ipaddr = ntohl(ipaddr);

	sprintf(buffer, "%d.%d.%d.%d",
		(ipaddr >> 24) & 0xff,
		(ipaddr >> 16) & 0xff,
		(ipaddr >>  8) & 0xff,
		(ipaddr      ) & 0xff);
	return buffer;
}



/*
 *	Internal wrapper for locking, to minimize the number of ifdef's
 *
 *	Lock an fd, prefer lockf() over flock()
 */
int rad_lockfd(int fd, int lock_len)
{
#if defined(F_LOCK) && !defined(BSD)
	return lockf(fd, F_LOCK, lock_len);
#elif defined(LOCK_EX)
	lock_len = lock_len;	/* -Wunused */
	return flock(fd, LOCK_EX);
#else
	struct flock fl;
	fl.l_start = 0;
	fl.l_len = lock_len;
	fl.l_pid = getpid();
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	return fcntl(fd, F_SETLKW, (void *)&fl);
#endif
}

/*
 *	Internal wrapper for locking, to minimize the number of ifdef's
 *
 *	Lock an fd, prefer lockf() over flock()
 *	Nonblocking version.
 */
int rad_lockfd_nonblock(int fd, int lock_len)
{
#if defined(F_LOCK) && !defined(BSD)
	return lockf(fd, F_TLOCK, lock_len);
#elif defined(LOCK_EX)
	lock_len = lock_len;	/* -Wunused */
	return flock(fd, LOCK_EX | LOCK_NB);
#else
	struct flock fl;
	fl.l_start = 0;
	fl.l_len = lock_len;
	fl.l_pid = getpid();
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	return fcntl(fd, F_SETLK, (void *)&fl);
#endif
}

/*
 *	Internal wrapper for unlocking, to minimize the number of ifdef's
 *	in the source.
 *
 *	Unlock an fd, prefer lockf() over flock()
 */
int rad_unlockfd(int fd, int lock_len)
{
#if defined(F_LOCK) && !defined(BSD)
	return lockf(fd, F_ULOCK, lock_len);
#elif defined(LOCK_EX)
	lock_len = lock_len;	/* -Wunused */
	return flock(fd, LOCK_UN);
#else
	struct flock fl;
	fl.l_start = 0;
	fl.l_len = lock_len;
	fl.l_pid = getpid();
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	return fcntl(fd, F_UNLCK, (void *)&fl);
#endif
}

/*
 *	Return an interface-id in standard colon notation
 */
char *ifid_ntoa(char *buffer, size_t size, uint8_t *ifid)
{
	snprintf(buffer, size, "%x:%x:%x:%x",
		 (ifid[0] << 8) + ifid[1], (ifid[2] << 8) + ifid[3],
		 (ifid[4] << 8) + ifid[5], (ifid[6] << 8) + ifid[7]);
	return buffer;
}


/*
 *	Return an interface-id from
 *	one supplied in standard colon notation.
 */
uint8_t *ifid_aton(const char *ifid_str, uint8_t *ifid)
{
	static const char xdigits[] = "0123456789abcdef";
	const char *p, *pch;
	int num_id = 0, val = 0, idx = 0;

	for (p = ifid_str; ; ++p) {
		if (*p == ':' || *p == '\0') {
			if (num_id <= 0)
				return NULL;

			/*
			 *	Drop 'val' into the array.
			 */
			ifid[idx] = (val >> 8) & 0xff;
			ifid[idx + 1] = val & 0xff;
			if (*p == '\0') {
				/*
				 *	Must have all entries before
				 *	end of the string.
				 */
				if (idx != 6)
					return NULL;
				break;
			}
			val = 0;
			num_id = 0;
			if ((idx += 2) > 6)
				return NULL;
		} else if ((pch = strchr(xdigits, tolower(*p))) != NULL) {
			if (++num_id > 4)
				return NULL;
			/*
			 *	Dumb version of 'scanf'
			 */
			val <<= 4;
			val |= (pch - xdigits);
		} else
			return NULL;
	}
	return ifid;
}


#ifndef HAVE_INET_PTON
static int inet_pton4(const char *src, struct in_addr *dst)
{
	int octet;
	unsigned int num;
	const char *p, *off;
	uint8_t tmp[4];
	static const char digits[] = "0123456789";

	octet = 0;
	p = src;
	while (1) {
		num = 0;
		while (*p && ((off = strchr(digits, *p)) != NULL)) {
			num *= 10;
			num += (off - digits);

			if (num > 255) return 0;

			p++;
		}
		if (!*p) break;

		/*
		 *	Not a digit, MUST be a dot, else we
		 *	die.
		 */
		if (*p != '.') {
			return 0;
		}

		tmp[octet++] = num;
		p++;
	}

	/*
	 *	End of the string.  At the fourth
	 *	octet is OK, anything else is an
	 *	error.
	 */
	if (octet != 3) {
		return 0;
	}
	tmp[3] = num;

	memcpy(dst, &tmp, sizeof(tmp));
	return 1;
}


#ifdef HAVE_STRUCT_SOCKADDR_IN6
/* int
 * inet_pton6(src, dst)
 *	convert presentation level address to network order binary form.
 * return:
 *	1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *	(1) does not touch `dst' unless it's returning 1.
 *	(2) :: in a full address is silently ignored.
 * credit:
 *	inspired by Mark Andrews.
 * author:
 *	Paul Vixie, 1996.
 */
static int
inet_pton6(const char *src, unsigned char *dst)
{
	static const char xdigits_l[] = "0123456789abcdef",
			  xdigits_u[] = "0123456789ABCDEF";
	u_char tmp[IN6ADDRSZ], *tp, *endp, *colonp;
	const char *xdigits, *curtok;
	int ch, saw_xdigit;
	u_int val;

	memset((tp = tmp), 0, IN6ADDRSZ);
	endp = tp + IN6ADDRSZ;
	colonp = NULL;
	/* Leading :: requires some special handling. */
	if (*src == ':')
		if (*++src != ':')
			return (0);
	curtok = src;
	saw_xdigit = 0;
	val = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
			pch = strchr((xdigits = xdigits_u), ch);
		if (pch != NULL) {
			val <<= 4;
			val |= (pch - xdigits);
			if (val > 0xffff)
				return (0);
			saw_xdigit = 1;
			continue;
		}
		if (ch == ':') {
			curtok = src;
			if (!saw_xdigit) {
				if (colonp)
					return (0);
				colonp = tp;
				continue;
			}
			if (tp + INT16SZ > endp)
				return (0);
			*tp++ = (u_char) (val >> 8) & 0xff;
			*tp++ = (u_char) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if (ch == '.' && ((tp + INADDRSZ) <= endp) &&
		    inet_pton4(curtok, (struct in_addr *) tp) > 0) {
			tp += INADDRSZ;
			saw_xdigit = 0;
			break;	/* '\0' was seen by inet_pton4(). */
		}
		return (0);
	}
	if (saw_xdigit) {
		if (tp + INT16SZ > endp)
			return (0);
		*tp++ = (u_char) (val >> 8) & 0xff;
		*tp++ = (u_char) val & 0xff;
	}
	if (colonp != NULL) {
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		const int n = tp - colonp;
		int i;

		for (i = 1; i <= n; i++) {
			endp[- i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if (tp != endp)
		return (0);
	/* bcopy(tmp, dst, IN6ADDRSZ); */
	memcpy(dst, tmp, IN6ADDRSZ);
	return (1);
}
#endif

/*
 *	Utility function, so that the rest of the server doesn't
 *	have ifdef's around IPv6 support
 */
int inet_pton(int af, const char *src, void *dst)
{
	if (af == AF_INET) {
		return inet_pton4(src, dst);
	}
#ifdef HAVE_STRUCT_SOCKADDR_IN6

	if (af == AF_INET6) {
		return inet_pton6(src, dst);
	}
#endif

	return -1;
}
#endif


#ifndef HAVE_INET_NTOP
/*
 *	Utility function, so that the rest of the server doesn't
 *	have ifdef's around IPv6 support
 */
const char *inet_ntop(int af, const void *src, char *dst, size_t cnt)
{
	if (af == AF_INET) {
		const uint8_t *ipaddr = src;

		if (cnt <= INET_ADDRSTRLEN) return NULL;

		snprintf(dst, cnt, "%d.%d.%d.%d",
			 ipaddr[0], ipaddr[1],
			 ipaddr[2], ipaddr[3]);
		return dst;
	}

	/*
	 *	If the system doesn't define this, we define it
	 *	in missing.h
	 */
	if (af == AF_INET6) {
		const struct in6_addr *ipaddr = src;

		if (cnt <= INET6_ADDRSTRLEN) return NULL;

		snprintf(dst, cnt, "%x:%x:%x:%x:%x:%x:%x:%x",
			 (ipaddr->s6_addr[0] << 8) | ipaddr->s6_addr[1],
			 (ipaddr->s6_addr[2] << 8) | ipaddr->s6_addr[3],
			 (ipaddr->s6_addr[4] << 8) | ipaddr->s6_addr[5],
			 (ipaddr->s6_addr[6] << 8) | ipaddr->s6_addr[7],
			 (ipaddr->s6_addr[8] << 8) | ipaddr->s6_addr[9],
			 (ipaddr->s6_addr[10] << 8) | ipaddr->s6_addr[11],
			 (ipaddr->s6_addr[12] << 8) | ipaddr->s6_addr[13],
			 (ipaddr->s6_addr[14] << 8) | ipaddr->s6_addr[15]);
		return dst;
	}

	return NULL;		/* don't support IPv6 */
}
#endif


/*
 *	Wrappers for IPv4/IPv6 host to IP address lookup.
 *	This API returns only one IP address, of the specified
 *	address family, or the first address (of whatever family),
 *	if AF_UNSPEC is used.
 */
int ip_hton(const char *src, int af, fr_ipaddr_t *dst)
{
	int error;
	struct addrinfo hints, *ai = NULL, *res = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;

	if ((error = getaddrinfo(src, NULL, &hints, &res)) != 0) {
		librad_log("ip_nton: %s", gai_strerror(error));
		return -1;
	}

	for (ai = res; ai; ai = ai->ai_next) {
		if ((af == ai->ai_family) || (af == AF_UNSPEC))
			break;
	}

	if (!ai) {
		librad_log("ip_hton failed to find requested information for host %.100s", src);
		freeaddrinfo(ai);
		return -1;
	}

	switch (ai->ai_family) {
	case AF_INET :
		dst->af = AF_INET;
		memcpy(&dst->ipaddr,
		       &((struct sockaddr_in*)ai->ai_addr)->sin_addr,
		       sizeof(struct in_addr));
		break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6 :
		dst->af = AF_INET6;
		memcpy(&dst->ipaddr,
		       &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr,
		       sizeof(struct in6_addr));
		break;
#endif

		/* Flow should never reach here */
	case AF_UNSPEC :
	default :
		librad_log("ip_hton found unusable information for host %.100s", src);
		freeaddrinfo(ai);
		return -1;
	}

	freeaddrinfo(ai);
	return 0;
}

/*
 *	Look IP addreses up, and print names (depending on DNS config)
 */
const char *ip_ntoh(const fr_ipaddr_t *src, char *dst, size_t cnt)
{
	struct sockaddr_storage ss;
	struct sockaddr_in  *s4;
	int error, len;

	/*
	 *	No DNS lookups
	 */
	if (!librad_dodns) {
		return inet_ntop(src->af, &(src->ipaddr), dst, cnt);
	}


	memset(&ss, 0, sizeof(ss));
        switch (src->af) {
        case AF_INET :
                s4 = (struct sockaddr_in *)&ss;
                len    = sizeof(struct sockaddr_in);
                s4->sin_family = AF_INET;
                s4->sin_port = 0;
                memcpy(&s4->sin_addr, &src->ipaddr.ip4addr, 4);
                break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
        case AF_INET6 :
		{
		struct sockaddr_in6 *s6;

                s6 = (struct sockaddr_in6 *)&ss;
                len    = sizeof(struct sockaddr_in6);
                s6->sin6_family = AF_INET6;
                s6->sin6_flowinfo = 0;
                s6->sin6_port = 0;
                memcpy(&s6->sin6_addr, &src->ipaddr.ip6addr, 16);
                break;
		}
#endif

        default :
                return NULL;
        }

	if ((error = getnameinfo((struct sockaddr *)&ss, len, dst, cnt, NULL, 0,
				 NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
		librad_log("ip_ntoh: %s", gai_strerror(error));
		return NULL;
	}
	return dst;
}


static const char *hextab = "0123456789abcdef";

/*
 *	hex2bin
 *
 *	We allow: hex == bin
 */
size_t fr_hex2bin(const char *hex, uint8_t *bin, size_t len)
{
	size_t i;
	char *c1, *c2;

	for (i = 0; i < len; i++) {
		if(!(c1 = memchr(hextab, tolower((int) hex[i << 1]), 16)) ||
		   !(c2 = memchr(hextab, tolower((int) hex[(i << 1) + 1]), 16)))
			break;
                 bin[i] = ((c1-hextab)<<4) + (c2-hextab);
	}

	return i;
}


/*
 *	bin2hex
 *
 *	If the output buffer isn't long enough, we have a buffer overflow.
 */
void fr_bin2hex(const uint8_t *bin, char *hex, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		hex[0] = hextab[((*bin) >> 4) & 0x0f];
		hex[1] = hextab[*bin & 0x0f];
		hex += 2;
		bin++;
	}
	*hex = '\0';
	return;
}


/*
 *	So we don't have ifdef's in the rest of the code
 */
#ifndef HAVE_CLOSEFROM
int closefrom(int fd)
{
	int i;
	int maxfd = 256;

#ifdef _SC_OPEN_MAX
	maxfd = sysconf(_SC_OPEN_MAX);
	if (maxfd < 0) {
	  maxfd = 256;
	}
#endif

	if (fd > maxfd) return 0;

	/*
	 *	FIXME: return EINTR?
	 *
	 *	Use F_CLOSEM?
	 */
	for (i = fd; i < maxfd; i++) {
		close(i);
	}

	return 0;
}
#endif

int fr_ipaddr_cmp(const fr_ipaddr_t *a, const fr_ipaddr_t *b)
{
	if (a->af < b->af) return -1;
	if (a->af > b->af) return +1;

	switch (a->af) {
	case AF_INET:
		return memcmp(&a->ipaddr.ip4addr,
			      &b->ipaddr.ip4addr,
			      sizeof(a->ipaddr.ip4addr));
		break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		return memcmp(&a->ipaddr.ip6addr,
			      &b->ipaddr.ip6addr,
			      sizeof(a->ipaddr.ip6addr));
		break;
#endif

	default:
		break;
	}

	return -1;
}
