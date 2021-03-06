/*
 * eap_peap.h
 *
 * Version:     $Id: eap_peap.h,v 1.10 2007/07/03 05:48:44 aland Exp $
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
 * Copyright 2003 Alan DeKok <aland@freeradius.org>
 * Copyright 2006 The FreeRADIUS server project
 */
#ifndef _EAP_PEAP_H
#define _EAP_PEAP_H

#include <freeradius-devel/ident.h>
RCSIDH(eap_peap_h, "$Id: eap_peap.h,v 1.10 2007/07/03 05:48:44 aland Exp $")

#include "eap_tls.h"

typedef struct peap_tunnel_t {
	VALUE_PAIR	*username;
	VALUE_PAIR	*state;
	VALUE_PAIR	*accept_vps;
	int		status;
	int		home_access_accept;
	int		default_eap_type;
	int		copy_request_to_tunnel;
	int		use_tunneled_reply;
	int		proxy_tunneled_request_as_eap;
	const char	*virtual_server;
} peap_tunnel_t;

#define PEAP_STATUS_START_PART2 0
#define PEAP_STATUS_SENT_TLV_SUCCESS 1
#define PEAP_STATUS_SENT_TLV_FAILURE 2

#define EAP_TLV_SUCCESS (1)
#define EAP_TLV_FAILURE (2)
#define EAP_TLV_ACK_RESULT (3)

#define PW_EAP_TLV 33

/*
 *	Process the PEAP portion of an EAP-PEAP request.
 */
int eappeap_process(EAP_HANDLER *handler, tls_session_t *tls_session);
#endif /* _EAP_PEAP_H */
