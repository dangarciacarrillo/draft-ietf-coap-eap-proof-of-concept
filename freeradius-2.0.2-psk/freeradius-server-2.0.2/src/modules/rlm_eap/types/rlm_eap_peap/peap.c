/*
 * peap.c  contains the interfaces that are called from eap
 *
 * Version:     $Id: peap.c,v 1.34 2007/11/27 15:05:25 aland Exp $
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
 *   Copyright 2003 Alan DeKok <aland@freeradius.org>
 *   Copyright 2006 The FreeRADIUS server project
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: peap.c,v 1.34 2007/11/27 15:05:25 aland Exp $")

#include "eap_peap.h"

/*
 *	Send protected EAP-Failure
 *
 *       Result-TLV = Failure
 */
static int eappeap_failure(EAP_HANDLER *handler, tls_session_t *tls_session)
{
	uint8_t tlv_packet[11];

	DEBUG2("  rlm_eap_peap: FAILURE");

	tlv_packet[0] = PW_EAP_REQUEST;
	tlv_packet[1] = handler->eap_ds->response->id +1;
	tlv_packet[2] = 0;
	tlv_packet[3] = 11;	/* length of this packet */
	tlv_packet[4] = PW_EAP_TLV;
	tlv_packet[5] = 0x80;
	tlv_packet[6] = EAP_TLV_ACK_RESULT;
	tlv_packet[7] = 0;
	tlv_packet[8] = 2;	/* length of the data portion */
	tlv_packet[9] = 0;
	tlv_packet[10] = EAP_TLV_FAILURE;

	(tls_session->record_plus)(&tls_session->clean_in, tlv_packet, 11);

	/*
	 *	FIXME: Check the return code.
	 */
	tls_handshake_send(tls_session);

	return 1;
}


/*
 *	Send protected EAP-Success
 *
 *       Result-TLV = Success
 */
static int eappeap_success(EAP_HANDLER *handler, tls_session_t *tls_session)
{
	uint8_t tlv_packet[11];

	DEBUG2("  rlm_eap_peap: SUCCESS");

	tlv_packet[0] = PW_EAP_REQUEST;
	tlv_packet[1] = handler->eap_ds->response->id +1;
	tlv_packet[2] = 0;
	tlv_packet[3] = 11;	/* length of this packet */
	tlv_packet[4] = PW_EAP_TLV;
	tlv_packet[5] = 0x80;	/* mandatory AVP */
	tlv_packet[6] = EAP_TLV_ACK_RESULT;
	tlv_packet[7] = 0;
	tlv_packet[8] = 2;	/* length of the data portion */
	tlv_packet[9] = 0;
	tlv_packet[10] = EAP_TLV_SUCCESS;

	(tls_session->record_plus)(&tls_session->clean_in, tlv_packet, 11);

	/*
	 *	FIXME: Check the return code.
	 */
	tls_handshake_send(tls_session);

	return 1;
}


/*
 *	Verify the tunneled EAP message.
 */
static int eapmessage_verify(const uint8_t *data, unsigned int data_len)
{
	const eap_packet_t *eap_packet = (const eap_packet_t *) data;
	uint8_t eap_type;
	char buffer[256];

	if (!data || (data_len <= 1)) {
		return 0;
	}

	eap_type = *data;
	switch (eap_type) {
	case PW_EAP_IDENTITY:
		DEBUG2("  rlm_eap_peap: Identity - %*s",
		       data_len - 1, data + 1);
		return 1;
		break;

		/*
		 *	If the first byte of the packet is
		 *	EAP-Response, and the EAP data is a TLV,
		 *	then it looks OK...
		 */
	case PW_EAP_RESPONSE:
		if (eap_packet->data[0] == PW_EAP_TLV) {
			DEBUG2("  rlm_eap_peap: Received EAP-TLV response.");
			return 1;
		}
		DEBUG2("  rlm_eap_peap: Got something weird.");
		break;


		/*
		 *	We normally do Microsoft MS-CHAPv2 (26), versus
		 *	Cisco MS-CHAPv2 (29).
		 */
	case PW_EAP_MSCHAPV2:
	default:
		DEBUG2("  rlm_eap_peap: EAP type %s",
		       eaptype_type2name(eap_type,
					 buffer, sizeof(buffer)));
		return 1;
		break;
	}

	return 0;
}

/*
 *	Convert a pseudo-EAP packet to a list of VALUE_PAIR's.
 */
static VALUE_PAIR *eap2vp(EAP_DS *eap_ds,
			  const uint8_t *data, size_t data_len)
{
	size_t total;
	VALUE_PAIR *vp = NULL, *head, **tail;

	if (data_len > 65535) return NULL; /* paranoia */

	vp = paircreate(PW_EAP_MESSAGE, PW_TYPE_OCTETS);
	if (!vp) {
		DEBUG2("  rlm_eap_peap: Failure in creating VP");
		return NULL;
	}

	total = data_len;
	if (total > 249) total = 249;

	/*
	 *	Hand-build an EAP packet from the crap in PEAP version 0.
	 */
	vp->vp_octets[0] = PW_EAP_RESPONSE;
	vp->vp_octets[1] = eap_ds->response->id;
	vp->vp_octets[2] = (data_len + EAP_HEADER_LEN) >> 8;
	vp->vp_octets[3] = (data_len + EAP_HEADER_LEN) & 0xff;

	memcpy(vp->vp_octets + EAP_HEADER_LEN, data, total);
	vp->length = EAP_HEADER_LEN + total;

	head = vp;
	tail = &(vp->next);
	while (total < data_len) {
		int vp_len;


		vp = paircreate(PW_EAP_MESSAGE, PW_TYPE_OCTETS);
		if (!vp) {
			DEBUG2("  rlm_eap_peap: Failure in creating VP");
			pairfree(&head);
			return NULL;
		}
		vp_len = (data_len - total);
		if (vp_len > 253) vp_len = 253;

		memcpy(vp->vp_octets, data + total, vp_len);
		vp->length = vp_len;
		
		total += vp_len;
		*tail = vp;
		tail = &(vp->next);
	}

	return head;
}


/*
 *	Convert a list of VALUE_PAIR's to an EAP packet, through the
 *	simple expedient of dumping the EAP message
 */
static int vp2eap(tls_session_t *tls_session, VALUE_PAIR *vp)
{
	/*
	 *	Skip the id, code, and length.  Just write the EAP
	 *	type & data to the client.
	 */
#ifndef NDEBUG
	if (debug_flag > 2) {
		size_t i, total;
		VALUE_PAIR *this;

		total = 0;

		for (this = vp; this != NULL; this = this->next) {
			int start = 0;

			if (this == vp) start = EAP_HEADER_LEN;
			
			for (i = start; i < vp->length; i++) {
				if ((total & 0x0f) == 0) printf("  PEAP tunnel data out %04x: ", total);

				printf("%02x ", vp->vp_octets[i]);
				
				if ((total & 0x0f) == 0x0f) printf("\n");
				total++;
			}
		}
		if ((total & 0x0f) != 0) printf("\n");
	}
#endif

	/*
	 *	Send the EAP data, WITHOUT the header.
	 */
	(tls_session->record_plus)(&tls_session->clean_in,
				   vp->vp_octets + EAP_HEADER_LEN,
				   vp->length - EAP_HEADER_LEN);
	
	/*
	 *	Send the rest of the EAP data.
	 */
	for (vp = vp->next; vp != NULL; vp = vp->next) {
		(tls_session->record_plus)(&tls_session->clean_in,
					   vp->vp_octets, vp->length);
	}

	tls_handshake_send(tls_session);

	return 1;
}


/*
 *	See if there's a TLV in the response.
 */
static int eappeap_check_tlv(const uint8_t *data)
{
	const eap_packet_t *eap_packet = (const eap_packet_t *) data;

	/*
	 *	Look for success or failure.
	 */
	if ((eap_packet->code == PW_EAP_RESPONSE) &&
	    (eap_packet->data[0] == PW_EAP_TLV)) {
		if (data[10] == EAP_TLV_SUCCESS) {
			return 1;
		}

		if (data[10] == EAP_TLV_FAILURE) {
			DEBUG2("  rlm_eap_peap: Client rejected our response.  The password is probably incorrect.");
			return 0;
		}
	}

	return 0;
}


/*
 *	Use a reply packet to determine what to do.
 */
static int process_reply(EAP_HANDLER *handler, tls_session_t *tls_session,
			 UNUSED REQUEST *request, RADIUS_PACKET *reply)
{
	int rcode = RLM_MODULE_REJECT;
	VALUE_PAIR *vp;
	peap_tunnel_t *t = tls_session->opaque;

#ifndef NDEBUG
	if (debug_flag > 0) {
		printf("  PEAP: Processing from tunneled session code %p %d\n",
		       reply, reply->code);

		for (vp = reply->vps; vp != NULL; vp = vp->next) {
			putchar('\t');vp_print(stdout, vp);putchar('\n');
		}
	}
#endif

	switch (reply->code) {
	case PW_AUTHENTICATION_ACK:
		DEBUG2("  PEAP: Tunneled authentication was successful.");
		t->status = PEAP_STATUS_SENT_TLV_SUCCESS;
		eappeap_success(handler, tls_session);
		rcode = RLM_MODULE_HANDLED;

		/*
		 *	If we've been told to use the attributes from
		 *	the reply, then do so.
		 *
		 *	WARNING: This may leak information about the
		 *	tunneled user!
		 */
		if (t->use_tunneled_reply) {
			DEBUG2("  Saving tunneled attributes for later");

			/*
			 *	Clean up the tunneled reply.
			 */
			pairdelete(&reply->vps, PW_PROXY_STATE);
			pairdelete(&reply->vps, PW_EAP_MESSAGE);
			pairdelete(&reply->vps, PW_MESSAGE_AUTHENTICATOR);

			t->accept_vps = reply->vps;
			reply->vps = NULL;
		}
		break;

	case PW_AUTHENTICATION_REJECT:
		DEBUG2("  PEAP: Tunneled authentication was rejected.");
		t->status = PEAP_STATUS_SENT_TLV_FAILURE;
		eappeap_failure(handler, tls_session);
		rcode = RLM_MODULE_HANDLED;
		break;

	case PW_ACCESS_CHALLENGE:
		DEBUG2("  PEAP: Got tunneled Access-Challenge");

		/*
		 *	Keep the State attribute, if necessary.
		 *
		 *	Get rid of the old State, too.
		 */
		pairfree(&t->state);
		pairmove2(&t->state, &(reply->vps), PW_STATE);

		/*
		 *	PEAP takes only EAP-Message attributes inside
		 *	of the tunnel.  Any Reply-Message in the
		 *	Access-Challenge is ignored.
		 */
		vp = NULL;
		pairmove2(&vp, &(reply->vps), PW_EAP_MESSAGE);

		/*
		 *	Handle EAP-MSCHAP-V2, where Access-Accept's
		 *	from the home server may contain MS-CHAP-Success,
		 *	which the module turns into challenges, so that
		 *	the client may respond to the challenge with
		 *	an "ack" packet.
		 */
		if (t->home_access_accept && t->use_tunneled_reply) {
			DEBUG2("  Saving tunneled attributes for later");

			/*
			 *	Clean up the tunneled reply.
			 */
			pairdelete(&reply->vps, PW_PROXY_STATE);
			pairdelete(&reply->vps, PW_MESSAGE_AUTHENTICATOR);

			t->accept_vps = reply->vps;
			reply->vps = NULL;
		}

		/*
		 *	Handle the ACK, by tunneling any necessary reply
		 *	VP's back to the client.
		 */
		if (vp) {
			vp2eap(tls_session, vp);
			pairfree(&vp);
		}

		rcode = RLM_MODULE_HANDLED;
		break;

	default:
		DEBUG2("  PEAP: Unknown RADIUS packet type %d: rejecting tunneled user", reply->code);
		rcode = RLM_MODULE_REJECT;
		break;
	}

	return rcode;
}


/*
 *	Do post-proxy processing,
 */
static int eappeap_postproxy(EAP_HANDLER *handler, void *data)
{
	int rcode;
	tls_session_t *tls_session = (tls_session_t *) data;
	REQUEST *fake;

	DEBUG2("  PEAP: Passing reply from proxy back into the tunnel.");

	/*
	 *	If there was a fake request associated with the proxied
	 *	request, do more processing of it.
	 */
	fake = (REQUEST *) request_data_get(handler->request,
					    handler->request->proxy,
					    REQUEST_DATA_EAP_MSCHAP_TUNNEL_CALLBACK);

	/*
	 *	Do the callback, if it exists, and if it was a success.
	 */
	if (fake && (handler->request->proxy_reply->code == PW_AUTHENTICATION_ACK)) {
		VALUE_PAIR *vp;
		REQUEST *request = handler->request;
		peap_tunnel_t *t = tls_session->opaque;

		t->home_access_accept = TRUE;

		/*
		 *	Terrible hacks.
		 */
		rad_assert(fake->packet == NULL);
		fake->packet = request->proxy;
		request->proxy = NULL;

		rad_assert(fake->reply == NULL);
		fake->reply = request->proxy_reply;
		request->proxy_reply = NULL;

		/*
		 *	Perform a post-auth stage, which will get the EAP
		 *	handler, too...
		 */
		fake->options &= ~RAD_REQUEST_OPTION_PROXY_EAP;
		DEBUG2("  PEAP: Passing reply back for EAP-MS-CHAP-V2 %p %d",
		       fake, fake->reply->code);
		rcode = module_post_proxy(0, fake);

		/*
		 *	FIXME: If rcode returns fail, do something
		 *	intelligent...
		 */
		DEBUG2("  POST-PROXY %d", rcode);
		rcode = rad_postauth(fake);
		DEBUG2("  POST-AUTH %d", rcode);

#ifndef NDEBUG
		if (debug_flag > 0) {
			printf("  PEAP: Final reply from tunneled session code %d\n",
			       fake->reply->code);

			for (vp = fake->reply->vps; vp != NULL; vp = vp->next) {
				putchar('\t');vp_print(stdout, vp);putchar('\n');
			}
		}
#endif

		/*
		 *	Terrible hacks.
		 */
		request->proxy = fake->packet;
		fake->packet = NULL;
		request->proxy_reply = fake->reply;
		fake->reply = NULL;

		/*
		 *	And we're done with this request.
		 */

		switch (rcode) {
                case RLM_MODULE_FAIL:
			request_free(&fake);
			eaptls_fail(handler->eap_ds, 0);
			return 0;
			break;

                default:  /* Don't Do Anything */
			DEBUG2(" PEAP: Got reply %d",
			       request->proxy_reply->code);
			break;
		}
	}
	request_free(&fake);	/* robust if fake == NULL */

	/*
	 *	If there was no EAP-Message in the reply packet, then
	 *	we know that we're supposed to re-run the "authenticate"
	 *	stage, in order to get the right kind of handling...
	 */

	/*
	 *	Process the reply from the home server.
	 */

	rcode = process_reply(handler, tls_session, handler->request,
			      handler->request->proxy_reply);

	/*
	 *	The proxy code uses the reply from the home server as
	 *	the basis for the reply to the NAS.  We don't want that,
	 *	so we toss it, after we've had our way with it.
	 */
	pairfree(&handler->request->proxy_reply->vps);

	switch (rcode) {
	case RLM_MODULE_REJECT:
		DEBUG2("  PEAP: Reply was rejected");
		eaptls_fail(handler->eap_ds, 0);
		return 0;

	case RLM_MODULE_HANDLED:
		DEBUG2("  PEAP: Reply was handled");
		eaptls_request(handler->eap_ds, tls_session);
		return 1;

	case RLM_MODULE_OK:
		DEBUG2("  PEAP: Reply was OK");
		eaptls_success(handler->eap_ds, 0);
		eaptls_gen_mppe_keys(&handler->request->reply->vps,
				     tls_session->ssl,
				     "client EAP encryption");
		return 1;

	default:
		DEBUG2("  PEAP: Reply was unknown.");
		break;
	}

	eaptls_fail(handler->eap_ds, 0);
	return 0;
}

/*
 *	Free a request.
 */
static void my_request_free(void *data)
{
	REQUEST *request = (REQUEST *)data;

	request_free(&request);
}


/*
 *	Process the pseudo-EAP contents of the tunneled data.
 */
int eappeap_process(EAP_HANDLER *handler, tls_session_t *tls_session)
{
	int err;
	peap_tunnel_t *t = tls_session->opaque;
	REQUEST *fake;
	VALUE_PAIR *vp;
	int rcode = RLM_MODULE_REJECT;
	const uint8_t	*data;
	unsigned int data_len;
#ifndef NDEBUG
	size_t i;
#endif

	REQUEST *request = handler->request;
	EAP_DS *eap_ds = handler->eap_ds;

	/*
	 *	FIXME: if the SSL session says "want read", or
	 *	similar, leave the data in the clean_out buffer.  This
	 *	lets the application data be sent across multiple
	 *	fragments.
	 */
	err = tls_handshake_recv(tls_session);
	if (!err) {
		DEBUG2(" rlm_eap_peap: Failed in SSL");
		return RLM_MODULE_REJECT;
	}

	/*
	 *	Just look at the buffer directly, without doing
	 *	record_minus.  This lets us avoid another data copy.
	 */
	data_len = tls_session->clean_out.used;
	tls_session->clean_out.used = 0;
	data = tls_session->clean_out.data;

#ifndef NDEBUG
	if (debug_flag > 2) {
		for (i = 0; i < data_len; i++) {
			if ((i & 0x0f) == 0) printf("  PEAP tunnel data in %04x: ", i);

			printf("%02x ", data[i]);

			if ((i & 0x0f) == 0x0f) printf("\n");
		}
		if ((data_len & 0x0f) != 0) printf("\n");
	}
#endif

	if (!eapmessage_verify(data, data_len)) {
		DEBUG2("  rlm_eap_peap: Tunneled data is invalid.");
		return RLM_MODULE_REJECT;
	}


	/*
	 *	If we authenticated the user, then it's OK.
	 */
	if (t->status == PEAP_STATUS_SENT_TLV_SUCCESS) {
		if (eappeap_check_tlv(data)) {
			DEBUG2("  rlm_eap_peap: Success");
			return RLM_MODULE_OK;
		}

		return RLM_MODULE_REJECT;

	} else if (t->status == PEAP_STATUS_SENT_TLV_FAILURE) {
		DEBUG2("  rlm_eap_peap:  Had sent TLV failure.  User was rejected earlier in this session.");
		return RLM_MODULE_REJECT;
	}

	fake = request_alloc_fake(request);

	rad_assert(fake->packet->vps == NULL);

	fake->packet->vps = eap2vp(eap_ds, data, data_len);
	if (!fake->packet->vps) {
		request_free(&fake);
		DEBUG2("  rlm_eap_peap: Unable to convert tunneled EAP packet to internal server data structures");
		return PW_AUTHENTICATION_REJECT;
	}

#ifndef NDEBUG
	if (debug_flag > 0) {
	  printf("  PEAP: Got tunneled EAP-Message\n");

	  for (vp = fake->packet->vps; vp != NULL; vp = vp->next) {
	    putchar('\t');vp_print(stdout, vp);putchar('\n');
	  }
	}
#endif

	/*
	 *	Tell the request that it's a fake one.
	 */
	vp = pairmake("Freeradius-Proxied-To", "127.0.0.1", T_OP_EQ);
	if (vp) {
		pairadd(&fake->packet->vps, vp);
	}

	/*
	 *	Update other items in the REQUEST data structure.
	 */
	if (!t->username) {
		/*
		 *	There's no User-Name in the tunneled session,
		 *	so we add one here, by pulling it out of the
		 *	EAP-Identity packet.
		 */
		if ((data[0] == PW_EAP_IDENTITY) && (data_len > 1)) {
			t->username = pairmake("User-Name", "", T_OP_EQ);
			rad_assert(t->username != NULL);

			memcpy(t->username->vp_strvalue, data + 1, data_len - 1);
			t->username->length = data_len - 1;
			t->username->vp_strvalue[t->username->length] = 0;
			DEBUG2("  PEAP: Got tunneled identity of %s", t->username->vp_strvalue);

			/*
			 *	If there's a default EAP type,
			 *	set it here.
			 */
			if (t->default_eap_type != 0) {
				DEBUG2("  PEAP: Setting default EAP type for tunneled EAP session.");
				vp = pairmake("EAP-Type", "0", T_OP_EQ);
				vp->vp_integer = t->default_eap_type;
				pairadd(&fake->config_items, vp);
			}
		}
	} /* else there WAS a t->username */

	if (t->username) {
		vp = paircopy(t->username);
		pairadd(&fake->packet->vps, vp);
		fake->username = pairfind(fake->packet->vps, PW_USER_NAME);
		DEBUG2("  PEAP: Setting User-Name to %s",
		       fake->username->vp_strvalue);
	}

	/*
	 *	Add the State attribute, too, if it exists.
	 */
	if (t->state) {
		vp = paircopy(t->state);
		if (vp) pairadd(&fake->packet->vps, vp);
	}

	/*
	 *	If this is set, we copy SOME of the request attributes
	 *	from outside of the tunnel to inside of the tunnel.
	 *
	 *	We copy ONLY those attributes which do NOT already
	 *	exist in the tunneled request.
	 *
	 *	This code is copied from ../rlm_eap_ttls/ttls.c
	 */
	if (t->copy_request_to_tunnel) {
		VALUE_PAIR *copy;

		for (vp = request->packet->vps; vp != NULL; vp = vp->next) {
			/*
			 *	The attribute is a server-side thingy,
			 *	don't copy it.
			 */
			if ((vp->attribute > 255) &&
			    (((vp->attribute >> 16) & 0xffff) == 0)) {
				continue;
			}

			/*
			 *	The outside attribute is already in the
			 *	tunnel, don't copy it.
			 *
			 *	This works for BOTH attributes which
			 *	are originally in the tunneled request,
			 *	AND attributes which are copied there
			 *	from below.
			 */
			if (pairfind(fake->packet->vps, vp->attribute)) {
				continue;
			}

			/*
			 *	Some attributes are handled specially.
			 */
			switch (vp->attribute) {
				/*
				 *	NEVER copy Message-Authenticator,
				 *	EAP-Message, or State.  They're
				 *	only for outside of the tunnel.
				 */
			case PW_USER_NAME:
			case PW_USER_PASSWORD:
			case PW_CHAP_PASSWORD:
			case PW_CHAP_CHALLENGE:
			case PW_PROXY_STATE:
			case PW_MESSAGE_AUTHENTICATOR:
			case PW_EAP_MESSAGE:
			case PW_STATE:
				continue;
				break;

				/*
				 *	By default, copy it over.
				 */
			default:
				break;
			}

			/*
			 *	Don't copy from the head, we've already
			 *	checked it.
			 */
			copy = paircopy2(vp, vp->attribute);
			pairadd(&fake->packet->vps, copy);
		}
	}

	if ((vp = pairfind(request->config_items, PW_VIRTUAL_SERVER)) != NULL) {
		fake->server = vp->vp_strvalue;

	} else if (t->virtual_server) {
		fake->server = t->virtual_server;

	} /* else fake->server == request->server */

#ifndef NDEBUG
	if (debug_flag > 0) {
		printf("  PEAP: Sending tunneled request\n");

		for (vp = fake->packet->vps; vp != NULL; vp = vp->next) {
			putchar('\t');vp_print(stdout, vp);putchar('\n');
		}

		printf("server %s {\n", fake->server);
	}
#endif

	/*
	 *	Call authentication recursively, which will
	 *	do PAP, CHAP, MS-CHAP, etc.
	 */
	rad_authenticate(fake);

	/*
	 *	Note that we don't do *anything* with the reply
	 *	attributes.
	 */
#ifndef NDEBUG
	if (debug_flag > 0) {
		printf("} # server %s\n", fake->server);

		printf("  PEAP: Got tunneled reply RADIUS code %d\n",
		 fake->reply->code);

		for (vp = fake->reply->vps; vp != NULL; vp = vp->next) {
			putchar('\t');vp_print(stdout, vp);putchar('\n');
		}
	}
#endif

	/*
	 *	Decide what to do with the reply.
	 */
	switch (fake->reply->code) {
	case 0:			/* No reply code, must be proxied... */
		vp = pairfind(fake->config_items, PW_PROXY_TO_REALM);

		if (vp) {
			eap_tunnel_data_t *tunnel;

			/*
			 *	The tunneled request was NOT handled,
			 *	it has to be proxied.  This means that
			 *	the "authenticate" stage was never
			 *	performed.
			 *
			 *	If we are told to NOT proxy the
			 *	tunneled request as EAP, then this
			 *	means that we've got to decode it,
			 *	which means that we MUST run the
			 *	"authenticate" portion by hand, here.
			 *
			 *	Once the tunneled EAP session is ALMOST
			 *	done, THEN we proxy it...
			 */
			if (!t->proxy_tunneled_request_as_eap) {
				fake->options |= RAD_REQUEST_OPTION_PROXY_EAP;

				/*
				 *	Hmm... should we check for
				 *	Auth-Type & EAP-Message here?
				 */


				/*
				 *	Run the EAP authentication.
				 */
				DEBUG2("  PEAP: Calling authenticate in order to initiate tunneled EAP session.");
				rcode = module_authenticate(PW_AUTHTYPE_EAP, fake);
				if (rcode == RLM_MODULE_OK) {
					/*
					 *	Authentication succeeded! Rah!
					 */
					fake->reply->code = PW_AUTHENTICATION_ACK;
					goto do_process;
				}

				if (rcode != RLM_MODULE_HANDLED) {
					DEBUG2("  PEAP: Can't handle the return code %d", rcode);
					rcode = RLM_MODULE_REJECT;
					goto done;
				}

				/*
				 *	The module decided it wasn't
				 *	done.  Handle it like normal.
				 */
				if ((fake->options & RAD_REQUEST_OPTION_PROXY_EAP) == 0) {
					DEBUG2("    PEAP: Cancelling proxy to realm %s until the tunneled EAP session has been established", vp->vp_strvalue);
					goto do_process;
				}

				/*
				 *	The module has decoded the
				 *	EAP-Message into another set
				 *	of attributes.
				 */
				pairdelete(&fake->packet->vps,
					   PW_EAP_MESSAGE);
			}

			DEBUG2("  PEAP: Tunneled authentication will be proxied to %s", vp->vp_strvalue);

			/*
			 *	Tell the original request that it's going
			 *	to be proxied.
			 */
			pairmove2(&(request->config_items),
				  &(fake->config_items),
				  PW_PROXY_TO_REALM);

			/*
			 *	Seed the proxy packet with the
			 *	tunneled request.
			 */
			rad_assert(request->proxy == NULL);
			request->proxy = fake->packet;
			fake->packet = NULL;
			rad_free(&fake->reply);
			fake->reply = NULL;

			/*
			 *	Set up the callbacks for the tunnel
			 */
			tunnel = rad_malloc(sizeof(*tunnel));
			memset(tunnel, 0, sizeof(*tunnel));

			tunnel->tls_session = tls_session;
			tunnel->callback = eappeap_postproxy;

			/*
			 *	Associate the callback with the request.
			 */
			rcode = request_data_add(request,
						 request->proxy,
						 REQUEST_DATA_EAP_TUNNEL_CALLBACK,
						 tunnel, free);
			rad_assert(rcode == 0);

			/*
			 *	We're not proxying it as EAP, so we've got
			 *	to do the callback later.
			 */
			if ((fake->options & RAD_REQUEST_OPTION_PROXY_EAP) != 0) {
				DEBUG2("  PEAP: Remembering to do EAP-MS-CHAP-V2 post-proxy.");

				/*
				 *	rlm_eap.c has taken care of associating
				 *	the handler with the fake request.
				 *
				 *	So we associate the fake request with
				 *	this request.
				 */
				rcode = request_data_add(request,
							 request->proxy,
							 REQUEST_DATA_EAP_MSCHAP_TUNNEL_CALLBACK,
							 fake, my_request_free);
				rad_assert(rcode == 0);

				/*
				 *	Do NOT free the fake request!
				 */
				return RLM_MODULE_UPDATED;
			}

			/*
			 *	Didn't authenticate the packet, but
			 *	we're proxying it.
			 */
			rcode = RLM_MODULE_UPDATED;

		} else {
			DEBUG2("  PEAP: Unknown RADIUS packet type %d: rejecting tunneled user", fake->reply->code);
			rcode = RLM_MODULE_REJECT;
		}
		break;

	default:
	do_process:
		rcode = process_reply(handler, tls_session, request,
				      fake->reply);
		break;
	}

 done:
	request_free(&fake);

	return rcode;
}
