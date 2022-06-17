#ifndef DETAIL_H
#define DETAIL_H
/*
 *	detail.h	Routines to handle detail files.
 *
 * Version:	$Id: detail.h,v 1.1 2007/08/25 04:06:59 aland Exp $
 *
 */

#include <freeradius-devel/ident.h>
RCSIDH(detail_h, "$Id: detail.h,v 1.1 2007/08/25 04:06:59 aland Exp $")

int detail_recv(rad_listen_t *listener,
		RAD_REQUEST_FUNP *pfun, REQUEST **prequest);
int detail_send(rad_listen_t *listener, REQUEST *request);
void detail_free(rad_listen_t *this);
int detail_print(rad_listen_t *this, char *buffer, size_t bufsize);
int detail_encode(UNUSED rad_listen_t *this, UNUSED REQUEST *request);
int detail_decode(UNUSED rad_listen_t *this, UNUSED REQUEST *request);
int detail_parse(CONF_SECTION *cs, rad_listen_t *this);

#endif /* DETAIL_H */
