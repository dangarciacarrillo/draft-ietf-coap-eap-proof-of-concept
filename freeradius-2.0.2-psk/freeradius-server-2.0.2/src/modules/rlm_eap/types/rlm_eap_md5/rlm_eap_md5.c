/*
 * rlm_eap_md5.c    Handles that are called from eap
 *
 * Version:     $Id: rlm_eap_md5.c,v 1.14 2007/12/25 08:17:26 aland Exp $
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
 * Copyright 2000,2001,2006  The FreeRADIUS server project
 * Copyright 2001  hereUare Communications, Inc. <raghud@hereuare.com>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: rlm_eap_md5.c,v 1.14 2007/12/25 08:17:26 aland Exp $")

#include <freeradius-devel/autoconf.h>

#include <stdio.h>
#include <stdlib.h>

#include "eap_md5.h"

#include <freeradius-devel/rad_assert.h>

/*
 *	Initiate the EAP-MD5 session by sending a challenge to the peer.
 */
static int md5_initiate(void *type_data, EAP_HANDLER *handler)
{
	int		i;
	MD5_PACKET	*reply;

	type_data = type_data;	/* -Wunused */

	/*
	 *	Allocate an EAP-MD5 packet.
	 */
	reply = eapmd5_alloc();
	if (reply == NULL)  {
		radlog(L_ERR, "rlm_eap_md5: out of memory");
		return 0;
	}

	/*
	 *	Fill it with data.
	 */
	reply->code = PW_MD5_CHALLENGE;
	reply->length = 1 + MD5_CHALLENGE_LEN; /* one byte of value size */
	reply->value_size = MD5_CHALLENGE_LEN;

	/*
	 *	Allocate user data.
	 */
	reply->value = malloc(reply->value_size);
	if (reply->value == NULL) {
		radlog(L_ERR, "rlm_eap_md5: out of memory");
		eapmd5_free(&reply);
		return 0;
	}

	/*
	 *	Get a random challenge.
	 */
	for (i = 0; i < reply->value_size; i++) {
		reply->value[i] = fr_rand();
	}
	DEBUG2("rlm_eap_md5: Issuing Challenge");

	/*
	 *	Keep track of the challenge.
	 */
	handler->opaque = malloc(reply->value_size);
	rad_assert(handler->opaque != NULL);
	memcpy(handler->opaque, reply->value, reply->value_size);
	handler->free_opaque = free;

	/*
	 *	Compose the EAP-MD5 packet out of the data structure,
	 *	and free it.
	 */
	eapmd5_compose(handler->eap_ds, reply);

	/*
	 *	We don't need to authorize the user at this point.
	 *
	 *	We also don't need to keep the challenge, as it's
	 *	stored in 'handler->eap_ds', which will be given back
	 *	to us...
	 */
	handler->stage = AUTHENTICATE;

	return 1;
}


/*
 *	Authenticate a previously sent challenge.
 */
static int md5_authenticate(UNUSED void *arg, EAP_HANDLER *handler)
{
	MD5_PACKET	*packet;
	MD5_PACKET	*reply;
	VALUE_PAIR	*password;

	/*
	 *	Get the Cleartext-Password for this user.
	 */
	rad_assert(handler->request != NULL);
	rad_assert(handler->stage == AUTHENTICATE);

	password = pairfind(handler->request->config_items, PW_CLEARTEXT_PASSWORD);
	if (password == NULL) {
		DEBUG2("rlm_eap_md5: Cleartext-Password is required for EAP-MD5 authentication");
		return 0;
	}

	/*
	 *	Extract the EAP-MD5 packet.
	 */
	if (!(packet = eapmd5_extract(handler->eap_ds)))
		return 0;

	/*
	 *	Create a reply, and initialize it.
	 */
	reply = eapmd5_alloc();
	if (!reply) {
		eapmd5_free(&packet);
		return 0;
	}
	reply->id = handler->eap_ds->request->id;
	reply->length = 0;

	/*
	 *	Verify the received packet against the previous packet
	 *	(i.e. challenge) which we sent out.
	 */
	if (eapmd5_verify(packet, password, handler->opaque)) {
		reply->code = PW_MD5_SUCCESS;
	} else {
		reply->code = PW_MD5_FAILURE;
	}

	/*
	 *	Compose the EAP-MD5 packet out of the data structure,
	 *	and free it.
	 */
	eapmd5_compose(handler->eap_ds, reply);

	eapmd5_free(&packet);
	return 1;
}

/*
 *	The module name should be the only globally exported symbol.
 *	That is, everything else should be 'static'.
 */
EAP_TYPE rlm_eap_md5 = {
	"eap_md5",
	NULL,				/* attach */
	md5_initiate,			/* Start the initial request */
	NULL,				/* authorization */
	md5_authenticate,		/* authentication */
	NULL				/* detach */
};
