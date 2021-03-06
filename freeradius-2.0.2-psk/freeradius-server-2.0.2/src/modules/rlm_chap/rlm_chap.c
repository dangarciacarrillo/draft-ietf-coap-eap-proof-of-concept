/*
 * rlm_chap.c
 *
 * Version:  $Id: rlm_chap.c,v 1.21 2007/11/11 22:02:02 aland Exp $
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
 * Copyright 2001,2006  The FreeRADIUS server project
 * Copyright 2001  Kostas Kalevras <kkalev@noc.ntua.gr>
 *
 * Nov 03 2001, Kostas Kalevras <kkalev@noc.ntua.gr>
 * - Added authorize() function to set Auth-Type if Chap-Password exists
 * - Added module messages when rejecting user
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: rlm_chap.c,v 1.21 2007/11/11 22:02:02 aland Exp $")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

static int chap_authorize(void *instance, REQUEST *request)
{

	/* quiet the compiler */
	instance = instance;
	request = request;

	if (!pairfind(request->packet->vps, PW_CHAP_PASSWORD)) {
		return RLM_MODULE_NOOP;
	}

	if (pairfind(request->config_items, PW_AUTHTYPE) != NULL) {
		DEBUG2("  rlm_chap: WARNING: Auth-Type already set.  Not setting to CHAP");
		return RLM_MODULE_NOOP;
	}

	DEBUG("  rlm_chap: Setting 'Auth-Type := CHAP'");
	pairadd(&request->config_items,
		pairmake("Auth-Type", "CHAP", T_OP_EQ));
	return RLM_MODULE_OK;
}


/*
 *	Find the named user in this modules database.  Create the set
 *	of attribute-value pairs to check and reply with for this user
 *	from the database. The authentication code only needs to check
 *	the password, the rest is done here.
 */
static int chap_authenticate(void *instance, REQUEST *request)
{
	VALUE_PAIR *passwd_item, *chap;
	uint8_t pass_str[MAX_STRING_LEN];
	VALUE_PAIR *module_fmsg_vp;
	char module_fmsg[MAX_STRING_LEN];

	/* quiet the compiler */
	instance = instance;
	request = request;

	if (!request->username) {
		radlog(L_AUTH, "rlm_chap: Attribute \"User-Name\" is required for authentication.\n");
		return RLM_MODULE_INVALID;
	}

	chap = pairfind(request->packet->vps, PW_CHAP_PASSWORD);
	if (!chap) {
		radlog(L_AUTH, "rlm_chap: Attribute \"CHAP-Password\" is required for authentication.");
		return RLM_MODULE_INVALID;
	}

	if (chap->length == 0) {
		radlog(L_ERR, "rlm_chap: empty password supplied");
		return RLM_MODULE_INVALID;
	}

	if (chap->length != CHAP_VALUE_LENGTH + 1) {
		radlog(L_ERR, "rlm_chap: password supplied has wrong length");
		return RLM_MODULE_INVALID;
	}

	/*
	 *	Don't print out the CHAP password here.  It's binary crap.
	 */
	DEBUG("  rlm_chap: login attempt by \"%s\" with CHAP password",
		request->username->vp_strvalue);

	if ((passwd_item = pairfind(request->config_items, PW_CLEARTEXT_PASSWORD)) == NULL){
	  DEBUG("  rlm_chap: Cleartext-Password is required for authentication");
		snprintf(module_fmsg, sizeof(module_fmsg),
			 "rlm_chap: Clear text password not available");
		module_fmsg_vp = pairmake("Module-Failure-Message",
					  module_fmsg, T_OP_EQ);
		pairadd(&request->packet->vps, module_fmsg_vp);
		return RLM_MODULE_INVALID;
	}

	DEBUG("  rlm_chap: Using clear text password \"%s\" for user %s authentication.",
	      passwd_item->vp_strvalue, request->username->vp_strvalue);

	rad_chap_encode(request->packet,pass_str,
			chap->vp_octets[0],passwd_item);

	if (memcmp(pass_str + 1, chap->vp_octets + 1,
		   CHAP_VALUE_LENGTH) != 0){
		DEBUG("  rlm_chap: Password check failed");
		snprintf(module_fmsg, sizeof(module_fmsg),
			 "rlm_chap: Wrong user password");
		module_fmsg_vp = pairmake("Module-Failure-Message",
					  module_fmsg, T_OP_EQ);
		pairadd(&request->packet->vps, module_fmsg_vp);
		return RLM_MODULE_REJECT;
	}

	DEBUG("  rlm_chap: chap user %s authenticated succesfully",
	      request->username->vp_strvalue);

	return RLM_MODULE_OK;
}

/*
 *	The module name should be the only globally exported symbol.
 *	That is, everything else should be 'static'.
 *
 *	If the module needs to temporarily modify it's instantiation
 *	data, the type should be changed to RLM_TYPE_THREAD_UNSAFE.
 *	The server will then take care of ensuring that the module
 *	is single-threaded.
 */
module_t rlm_chap = {
	 RLM_MODULE_INIT,
	"CHAP",
	RLM_TYPE_CHECK_CONFIG_SAFE,   	/* type */
	NULL,				/* instantiation */
	NULL,				/* detach */
	{
		chap_authenticate,	/* authentication */
		chap_authorize,	 	/* authorization */
		NULL,			/* preaccounting */
		NULL,			/* accounting */
		NULL,			/* checksimul */
		NULL,			/* pre-proxy */
		NULL,			/* post-proxy */
		NULL			/* post-auth */
	},
};
