/*
 * eap_md5.c  EAP MD5 functionality.
 *
 * Version:     $Id: eap_md5.c,v 1.15 2007/11/23 12:58:12 aland Exp $
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

/*
 *
 *  MD5 Packet Format in EAP Type-Data
 *  --- ------ ------ -- --- ---------
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Value-Size   |  Value ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Name ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: eap_md5.c,v 1.15 2007/11/23 12:58:12 aland Exp $")

#include <stdio.h>
#include <stdlib.h>
#include "eap.h"

#include "eap_md5.h"

/*
 *      Allocate a new MD5_PACKET
 */
MD5_PACKET *eapmd5_alloc(void)
{
	MD5_PACKET   *rp;

	if ((rp = malloc(sizeof(MD5_PACKET))) == NULL) {
		radlog(L_ERR, "rlm_eap_md5: out of memory");
		return NULL;
	}
	memset(rp, 0, sizeof(MD5_PACKET));
	return rp;
}

/*
 *      Free MD5_PACKET
 */
void eapmd5_free(MD5_PACKET **md5_packet_ptr)
{
	MD5_PACKET *md5_packet;

	if (!md5_packet_ptr) return;
	md5_packet = *md5_packet_ptr;
	if (md5_packet == NULL) return;

	if (md5_packet->value) free(md5_packet->value);
	if (md5_packet->name) free(md5_packet->name);

	free(md5_packet);

	*md5_packet_ptr = NULL;
}

/*
 *	We expect only RESPONSE for which SUCCESS or FAILURE is sent back
 */
MD5_PACKET *eapmd5_extract(EAP_DS *eap_ds)
{
	md5_packet_t	*data;
	MD5_PACKET	*packet;
	unsigned short	name_len;

	/*
	 *	We need a response, of type EAP-MD5, with at least
	 *	one byte of type data (EAP-MD5) following the 4-byte
	 *	EAP-Packet header.
	 */
	if (!eap_ds 					 ||
	    !eap_ds->response 				 ||
	    (eap_ds->response->code != PW_MD5_RESPONSE)	 ||
	    eap_ds->response->type.type != PW_EAP_MD5	 ||
	    !eap_ds->response->type.data 		 ||
	    (eap_ds->response->length <= MD5_HEADER_LEN) ||
	    (eap_ds->response->type.data[0] <= 0)) {
		radlog(L_ERR, "rlm_eap_md5: corrupted data");
		return NULL;
	}

	packet = eapmd5_alloc();
	if (!packet) return NULL;

	/*
	 *	Code & id for MD5 & EAP are same
	 *
	 *	but md5_length = length of the EAP-MD5 data, which
	 *	doesn't include the EAP header, or the octet saying
	 *	EAP-MD5.
	 */
	packet->code = eap_ds->response->code;
	packet->id = eap_ds->response->id;
	packet->length = eap_ds->response->length - (MD5_HEADER_LEN + 1);

	/*
	 *	Sanity check the EAP-MD5 packet sent to us
	 *	by the client.
	 */
	data = (md5_packet_t *)eap_ds->response->type.data;

	/*
	 *	Already checked the size above.
	 */
	packet->value_size = data->value_size;

	/*
	 *	Allocate room for the data, and copy over the data.
	 */
	packet->value = malloc(packet->value_size);
	if (packet->value == NULL) {
		radlog(L_ERR, "rlm_eap_md5: out of memory");
		eapmd5_free(&packet);
		return NULL;
	}
	memcpy(packet->value, data->value_name, packet->value_size);

	/*
	 *	Name is optional and is present after Value, but we
	 *	need to check for it, as eapmd5_compose()
	 */
	name_len =  packet->length - (packet->value_size + 1);
	if (name_len) {
		packet->name = malloc(name_len + 1);
		if (!packet->name) {
			radlog(L_ERR, "rlm_eap_md5: out of memory");
			eapmd5_free(&packet);
			return NULL;
		}
		memcpy(packet->name, data->value_name + packet->value_size,
		       name_len);
		packet->name[name_len] = 0;
	}

	return packet;
}


/*
 * verify = MD5(id+password+challenge_sent)
 */
int eapmd5_verify(MD5_PACKET *packet, VALUE_PAIR* password,
		  uint8_t *challenge)
{
	char	*ptr;
	char	string[1 + MAX_STRING_LEN*2];
	unsigned char output[MAX_STRING_LEN];
	unsigned short len;

	/*
	 *	Sanity check it.
	 */
	if (packet->value_size != 16) {
		radlog(L_ERR, "rlm_eap_md5: Expected 16 bytes of response to challenge, got %d", packet->value_size);
		return 0;
	}

	len = 0;
	ptr = string;

	/*
	 *	This is really rad_chap_pwencode()...
	 */
	*ptr++ = packet->id;
	len++;
	memcpy(ptr, password->vp_strvalue, password->length);
	ptr += password->length;
	len += password->length;

	/*
	 *	The challenge size is hard-coded.
	 */
	memcpy(ptr, challenge, MD5_CHALLENGE_LEN);
	len += MD5_CHALLENGE_LEN;

	fr_md5_calc((u_char *)output, (u_char *)string, len);

	/*
	 *	The length of the response is always 16 for MD5.
	 */
	if (memcmp(output, packet->value, 16) != 0) {
		return 0;
	}
	return 1;
}

/*
 *	Compose the portions of the reply packet specific to the
 *	EAP-MD5 protocol, in the EAP reply typedata
 */
int eapmd5_compose(EAP_DS *eap_ds, MD5_PACKET *reply)
{
	uint8_t *ptr;
	unsigned short name_len;

	/*
	 *	We really only send Challenge (EAP-Identity),
	 *	and EAP-Success, and EAP-Failure.
	 */
	if (reply->code < 3) {
		eap_ds->request->type.type = PW_EAP_MD5;

		rad_assert(reply->length > 0);

		eap_ds->request->type.data = malloc(reply->length);
		if (eap_ds->request->type.data == NULL) {
			radlog(L_ERR, "rlm_eap_md5: out of memory");
			return 0;
		}
		ptr = eap_ds->request->type.data;
		*ptr++ = (uint8_t)(reply->value_size & 0xFF);
		memcpy(ptr, reply->value, reply->value_size);

		/* Just the Challenge length */
		eap_ds->request->type.length = reply->value_size + 1;

		/*
		 *	Return the name, if necessary.
		 *
		 *	Don't see why this is *ever* necessary...
		 */
		name_len = reply->length - (reply->value_size + 1);
		if (name_len && reply->name) {
			ptr += reply->value_size;
			memcpy(ptr, reply->name, name_len);
			/* Challenge length + Name length */
			eap_ds->request->type.length += name_len;
		}
	} else {
		eap_ds->request->type.length = 0;
		/* TODO: In future we might add message here wrt rfc1994 */
	}
	eap_ds->request->code = reply->code;

	eapmd5_free(&reply);

	return 1;
}
