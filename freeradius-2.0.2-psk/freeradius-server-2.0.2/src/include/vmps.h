#ifndef VMPS_H
#define VMPS_H
/*
 *	vmps.h	Routines to handle VMPS sockets.
 *
 * Version:	$Id: vmps.h,v 1.1 2007/08/25 03:58:40 aland Exp $
 *
 */

#include <freeradius-devel/ident.h>
RCSIDH(vmps_h, "$Id: vmps.h,v 1.1 2007/08/25 03:58:40 aland Exp $")

int vqp_socket_recv(rad_listen_t *listener,
		    RAD_REQUEST_FUNP *pfun, REQUEST **prequest);
int vqp_socket_send(rad_listen_t *listener, REQUEST *request);
int vqp_socket_encode(UNUSED rad_listen_t *listener, REQUEST *request);
int vqp_socket_decode(UNUSED rad_listen_t *listener, REQUEST *request);
int vmps_process(REQUEST *request);

#endif /* VMPS_H */
