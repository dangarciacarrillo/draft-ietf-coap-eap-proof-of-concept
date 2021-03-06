/* Copyright 2006 The FreeRADIUS server project */

#ifndef _SMBDES_H
#define _SMBDES_H

#include <freeradius-devel/ident.h>
RCSIDH(smbdes_h, "$Id: smbdes.h,v 1.3 2007/11/25 14:02:11 aland Exp $")

void smbdes_lmpwdhash(const char *password, uint8_t *lmhash);
void smbdes_mschap(const char *win_password,
		 const uint8_t *challenge, uint8_t *response);

#endif /*_SMBDES_H*/
