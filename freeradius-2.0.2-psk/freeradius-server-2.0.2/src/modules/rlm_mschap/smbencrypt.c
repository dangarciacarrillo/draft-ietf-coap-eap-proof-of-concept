/*
 * smbencrypt.c	Produces LM-Password and NT-Password from
 *		cleartext password
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
 * Copyright 2002  3APA3A for FreeRADIUS project
   Copyright 2006  The FreeRADIUS server project
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: smbencrypt.c,v 1.13 2007/11/25 14:02:11 aland Exp $")

#include	<freeradius-devel/libradius.h>
#include        <freeradius-devel/md4.h>
#include        <ctype.h>


#include	"smbdes.h"

static const char * hex = "0123456789ABCDEF";

/*
 *	FIXME: use functions in freeradius
 */
static void tohex (const unsigned char * src, size_t len, char *dst)
{
	size_t i;
	for (i=0; i<len; i++) {
		dst[(i*2)] = hex[(src[i] >> 4)];
		dst[(i*2) + 1] = hex[(src[i]&0x0F)];
	}
	dst[(i*2)] = 0;
}

static void ntpwdhash (uint8_t *szHash, const char *szPassword)
{
	char szUnicodePass[513];
	char nPasswordLen;
	int i;

	/*
	 *	NT passwords are unicode.  Convert plain text password
	 *	to unicode by inserting a zero every other byte
	 */
	nPasswordLen = strlen(szPassword);
	for (i = 0; i < nPasswordLen; i++) {
		szUnicodePass[i << 1] = szPassword[i];
		szUnicodePass[(i << 1) + 1] = 0;
	}

	/* Encrypt Unicode password to a 16-byte MD4 hash */
	fr_md4_calc(szHash, (uint8_t *) szUnicodePass, (nPasswordLen<<1) );
}



int main (int argc, char *argv[])
{
	int i, l;
	char password[1024];
	uint8_t hash[16];
	char ntpass[33];
	char lmpass[33];

	fprintf(stderr, "LM Hash                         \tNT Hash\n");
	fprintf(stderr, "--------------------------------\t--------------------------------\n");
	fflush(stderr);
	for (i = 1; i < argc; i++ ) {
		l = strlen(password);
		if (l && password[l-1] == '\n') password [l-1] = 0;
		smbdes_lmpwdhash(argv[i], hash);
		tohex (hash, 16, lmpass);
		ntpwdhash (hash, argv[i]);
		tohex (hash, 16, ntpass);
		printf("%s\t%s\n", lmpass, ntpass);
	}
	return 0;
}
