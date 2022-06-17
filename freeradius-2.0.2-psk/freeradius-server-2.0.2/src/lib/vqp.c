/*
 * vqp.c	Functions to send/receive VQP packets.
 *
 * Version:	$Id: vqp.c,v 1.4 2008/01/05 17:58:44 nbk Exp $
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
 * Copyright 2007 Alan DeKok <aland@deployingradius.com>
 */

#include	<freeradius-devel/ident.h>
RCSID("$Id: vqp.c,v 1.4 2008/01/05 17:58:44 nbk Exp $");

#include	<freeradius-devel/libradius.h>
#include	<freeradius-devel/udpfromto.h>
#include	<freeradius-devel/vqp.h>

#ifdef WITH_VMPS

/*
 *  http://www.openbsd.org/cgi-bin/cvsweb/src/usr.sbin/tcpdump/print-vqp.c
 *
 *  Some of how it works:
 *
 *  http://www.hackingciscoexposed.com/pdf/chapter12.pdf
 *
 * VLAN Query Protocol (VQP)
 *
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |    Version    |    Opcode     | Response Code |  Data Count   |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                         Transaction ID                        |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                            Type (1)                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |             Length            |            Data               /
 *   /                                                               /
 *   /                                                               /
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                            Type (n)                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |             Length            |            Data               /
 *   /                                                               /
 *   /                                                               /
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * VQP is layered over UDP.  The default destination port is 1589.
 *
 */
#define VQP_HDR_LEN (8)
#define VQP_VERSION (1)
#define VQP_MAX_ATTRIBUTES (12)


/*
 *	Wrapper for sendto which handles sendfromto, IPv6, and all
 *	possible combinations.
 *
 *	FIXME:  This is just a copy of rad_sendto().
 *	Duplicate code is bad.
 */
static int vqp_sendto(int sockfd, void *data, size_t data_len, int flags,
		      fr_ipaddr_t *src_ipaddr, fr_ipaddr_t *dst_ipaddr,
		      int dst_port)
{
	struct sockaddr_storage	dst;
	socklen_t		sizeof_dst = sizeof(dst);

#ifdef WITH_UDPFROMTO
	struct sockaddr_storage	src;
	socklen_t		sizeof_src = sizeof(src);

	memset(&src, 0, sizeof(src));
#endif
	memset(&dst, 0, sizeof(dst));

	/*
	 *	IPv4 is supported.
	 */
	if (dst_ipaddr->af == AF_INET) {
		struct sockaddr_in	*s4;

		s4 = (struct sockaddr_in *)&dst;
		sizeof_dst = sizeof(struct sockaddr_in);

		s4->sin_family = AF_INET;
		s4->sin_addr = dst_ipaddr->ipaddr.ip4addr;
		s4->sin_port = htons(dst_port);

#ifdef WITH_UDPFROMTO
		s4 = (struct sockaddr_in *)&src;
		sizeof_src = sizeof(struct sockaddr_in);

		s4->sin_family = AF_INET;
		s4->sin_addr = src_ipaddr->ipaddr.ip4addr;
#endif

	/*
	 *	IPv6 MAY be supported.
	 */
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	} else if (dst_ipaddr->af == AF_INET6) {
		struct sockaddr_in6	*s6;

		s6 = (struct sockaddr_in6 *)&dst;
		sizeof_dst = sizeof(struct sockaddr_in6);
		
		s6->sin6_family = AF_INET6;
		s6->sin6_addr = dst_ipaddr->ipaddr.ip6addr;
		s6->sin6_port = htons(dst_port);

#ifdef WITH_UDPFROMTO
		return -1;	/* UDPFROMTO && IPv6 are not supported */
#if 0
		s6 = (struct sockaddr_in6 *)&src;
		sizeof_src = sizeof(struct sockaddr_in6);

		s6->sin6_family = AF_INET6;
		s6->sin6_addr = src_ipaddr->ipaddr.ip6addr;
#endif /* #if 0 */
#endif /* WITH_UDPFROMTO */
#endif /* HAVE_STRUCT_SOCKADDR_IN6 */
	} else return -1;   /* Unknown address family, Die Die Die! */

#ifdef WITH_UDPFROMTO
	/*
	 *	Only IPv4 is supported for udpfromto.
	 *
	 *	And if they don't specify a source IP address, don't
	 *	use udpfromto.
	 */
	if ((dst_ipaddr->af == AF_INET) ||
	    (src_ipaddr->af != AF_UNSPEC)) {
		return sendfromto(sockfd, data, data_len, flags,
				  (struct sockaddr *)&src, sizeof_src, 
				  (struct sockaddr *)&dst, sizeof_dst);
	}
#else
	src_ipaddr = src_ipaddr; /* -Wunused */
#endif

	/*
	 *	No udpfromto, OR an IPv6 socket, fail gracefully.
	 */
	return sendto(sockfd, data, data_len, flags, 
		      (struct sockaddr *)&dst, sizeof_dst);
}

/*
 *	Wrapper for recvfrom, which handles recvfromto, IPv6, and all
 *	possible combinations.
 *
 *	FIXME:  This is copied from rad_recvfrom, with minor edits.
 */
static ssize_t vqp_recvfrom(int sockfd, uint8_t **pbuf, int flags,
			    fr_ipaddr_t *src_ipaddr, uint16_t *src_port,
			    fr_ipaddr_t *dst_ipaddr, uint16_t *dst_port)
{
	struct sockaddr_storage	src;
	struct sockaddr_storage	dst;
	socklen_t		sizeof_src = sizeof(src);
	socklen_t	        sizeof_dst = sizeof(dst);
	ssize_t			data_len;
	uint8_t			header[4];
	void			*buf;
	size_t			len;

	memset(&src, 0, sizeof_src);
	memset(&dst, 0, sizeof_dst);

	/*
	 *	Get address family, etc. first, so we know if we
	 *	need to do udpfromto.
	 *
	 *	FIXME: udpfromto also does this, but it's not
	 *	a critical problem.
	 */
	if (getsockname(sockfd, (struct sockaddr *)&dst,
			&sizeof_dst) < 0) return -1;

	/*
	 *	Read the length of the packet, from the packet.
	 *	This lets us allocate the buffer to use for
	 *	reading the rest of the packet.
	 */
	data_len = recvfrom(sockfd, header, sizeof(header), MSG_PEEK,
			    (struct sockaddr *)&src, &sizeof_src);
	if (data_len < 0) return -1;

	/*
	 *	Too little data is available, discard the packet.
	 */
	if (data_len < 4) {
		recvfrom(sockfd, header, sizeof(header), flags, 
			 (struct sockaddr *)&src, &sizeof_src);
		return 0;

		/*
		 *	Invalid version, packet type, or too many
		 *	attributes.  Die.
		 */
	} else if ((header[0] != VQP_VERSION) ||
		   (header[1] < 1) ||
		   (header[1] > 4) ||
		   (header[3] > VQP_MAX_ATTRIBUTES)) {
		recvfrom(sockfd, header, sizeof(header), flags,
			 (struct sockaddr *)&src, &sizeof_src);
		return 0;

	} else {		/* we got 4 bytes of data. */
		/*
		 *	We don't care about the contents for now...
		 */
#if 0
		/*
		 *	How many attributes are in the packet.
		 */
		len = header[3];

		if ((header[1] == 1) || (header[1] == 3)) {
			if (len != VQP_MAX_ATTRIBUTES) {
				recvfrom(sockfd, header, sizeof(header), 0,
					 (struct sockaddr *)&src, &sizeof_src);
				return 0;
			}
			/*
			 *	Maximum length we support.
			 */
			len = (12 * (4 + 4 + 253));

		} else {
			if (len != 2) {
				recvfrom(sockfd, header, sizeof(header), 0, 
				 (struct sockaddr *)&src, &sizeof_src);
				return 0;
			}
			/*
			 *	Maximum length we support.
			 */
			len = (12 * (4 + 4 + 253));
		}
#endif
	}

	/*
	 *	For now, be generous.
	 */
	len = (12 * (4 + 4 + 253));

	buf = malloc(len);
	if (!buf) return -1;

	/*
	 *	Receive the packet.  The OS will discard any data in the
	 *	packet after "len" bytes.
	 */
#ifdef WITH_UDPFROMTO
	if (dst.ss_family == AF_INET) {
		data_len = recvfromto(sockfd, buf, len, flags,
				      (struct sockaddr *)&src, &sizeof_src, 
				      (struct sockaddr *)&dst, &sizeof_dst);
	} else
#endif
		/*
		 *	No udpfromto, OR an IPv6 socket.  Fail gracefully.
		 */
		data_len = recvfrom(sockfd, buf, len, flags, 
				    (struct sockaddr *)&src, &sizeof_src);
	if (data_len < 0) {
		free(buf);
		return data_len;
	}

	/*
	 *	Check address families, and update src/dst ports, etc.
	 */
	if (src.ss_family == AF_INET) {
		struct sockaddr_in	*s4;

		s4 = (struct sockaddr_in *)&src;
		src_ipaddr->af = AF_INET;
		src_ipaddr->ipaddr.ip4addr = s4->sin_addr;
		*src_port = ntohs(s4->sin_port);

		s4 = (struct sockaddr_in *)&dst;
		dst_ipaddr->af = AF_INET;
		dst_ipaddr->ipaddr.ip4addr = s4->sin_addr;
		*dst_port = ntohs(s4->sin_port);

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	} else if (src.ss_family == AF_INET6) {
		struct sockaddr_in6	*s6;

		s6 = (struct sockaddr_in6 *)&src;
		src_ipaddr->af = AF_INET6;
		src_ipaddr->ipaddr.ip6addr = s6->sin6_addr;
		*src_port = ntohs(s6->sin6_port);

		s6 = (struct sockaddr_in6 *)&dst;
		dst_ipaddr->af = AF_INET6;
		dst_ipaddr->ipaddr.ip6addr = s6->sin6_addr;
		*dst_port = ntohs(s6->sin6_port);
#endif
	} else {
		free(buf);
		return -1;	/* Unknown address family, Die Die Die! */
	}
	
	/*
	 *	Different address families should never happen.
	 */
	if (src.ss_family != dst.ss_family) {
		free(buf);
		return -1;
	}

	/*
	 *	Tell the caller about the data
	 */
	*pbuf = buf;

	return data_len;
}

RADIUS_PACKET *vqp_recv(int sockfd)
{
	uint8_t *ptr;
	ssize_t length;
	uint32_t id;
	RADIUS_PACKET *packet;

	/*
	 *	Allocate the new request data structure
	 */
	if ((packet = malloc(sizeof(*packet))) == NULL) {
		librad_log("out of memory");
		return NULL;
	}
	memset(packet, 0, sizeof(*packet));

	packet->data_len = vqp_recvfrom(sockfd, &packet->data, 0,
					&packet->src_ipaddr, &packet->src_port,
					&packet->dst_ipaddr, &packet->dst_port);

	/*
	 *	Check for socket errors.
	 */
	if (packet->data_len < 0) {
		librad_log("Error receiving packet: %s", strerror(errno));
		/* packet->data is NULL */
		free(packet);
		return NULL;
	}


	/*
	 *	We can only receive packets formatted in a way we
	 *	expect.  However, we accept MORE attributes in a
	 *	packet than normal implementations may send.
	 */
	if (packet->data_len < VQP_HDR_LEN) {
		librad_log("VQP packet is too short");
		rad_free(&packet);
		return NULL;
	}

	ptr = packet->data;

	if (0) {
		int i;
		for (i = 0; i < packet->data_len; i++) {
			if ((i & 0x0f) == 0) fprintf(stderr, "%02x: ", i);
			fprintf(stderr, "%02x ", ptr[i]);
			if ((i & 0x0f) == 0x0f) fprintf(stderr, "\n");
		}
	  
	}

	if (ptr[3] > VQP_MAX_ATTRIBUTES) {
		librad_log("Too many VQP attributes");
		rad_free(&packet);
		return NULL;
	}

	if (packet->data_len > VQP_HDR_LEN) {
		int attrlen;

		/*
		 *	Skip the header.
		 */
		ptr += VQP_HDR_LEN;
		length = packet->data_len - VQP_HDR_LEN;

		while (length > 0) {
			if (length < 7) {
				librad_log("Packet contains malformed attribute");
				rad_free(&packet);
				return NULL;
			}

			/*
			 *	Attributes are 4 bytes
			 *	0x00000c01 ... 0x00000c08
			 */
			if ((ptr[0] != 0) || (ptr[1] != 0) ||
			    (ptr[2] != 0x0c) || (ptr[3] < 1) || (ptr[3] > 8)) {
				librad_log("Packet contains invalid attribute");
				rad_free(&packet);
				return NULL;
			}
			
			/*
			 *	Length is 2 bytes
			 *
			 *	We support lengths 1..253, for internal
			 *	server reasons.  Also, there's no reason
			 *	for bigger lengths to exist... admins
			 *	won't be typing in a 32K vlan name.
			 */
			if ((ptr[4] != 0) || (ptr[5] > 253)) {
				librad_log("Packet contains attribute with invalid length %02x %02x", ptr[4], ptr[5]);
				rad_free(&packet);
				return NULL;
			}
			attrlen = ptr[5];
			ptr += 6 + attrlen;
			length -= (6 + attrlen);
		}
	}

	packet->sockfd = sockfd;
	packet->vps = NULL;

	/*
	 *	This is more than a bit of a hack.
	 */
	packet->code = PW_AUTHENTICATION_REQUEST;

	memcpy(&id, packet->data + 4, 4);
	packet->id = ntohl(id);

	/*
	 *	FIXME: Create a fake "request authenticator", to
	 *	avoid duplicates?  Or is the VQP sequence number
	 *	adequate for this purpose?
	 */

	return packet;
}

/*
 *	We do NOT  mirror the old-style RADIUS code  that does encode,
 *	sign && send in one function.  For VQP, the caller MUST perform
 *	each task manually, and separately.
 */
int vqp_send(RADIUS_PACKET *packet)
{
	if (!packet || !packet->data || (packet->data_len < 8)) return -1;

	/*
	 *	Don't print out the attributes, they were printed out
	 *	when it was encoded.
	 */

	/*
	 *	And send it on it's way.
	 */
	return vqp_sendto(packet->sockfd, packet->data, packet->data_len, 0,
			  &packet->src_ipaddr, &packet->dst_ipaddr,
			  packet->dst_port);
}


int vqp_decode(RADIUS_PACKET *packet)
{
	uint8_t *ptr, *end;
	int attribute, length;
	VALUE_PAIR *vp, **tail;

	if (!packet || !packet->data) return -1;

	if (packet->data_len < VQP_HDR_LEN) return -1;

	tail = &packet->vps;

	vp = paircreate(PW_VQP_PACKET_TYPE, PW_TYPE_OCTETS);
	if (!vp) {
		librad_log("No memory");
		return -1;
	}
	vp->lvalue = packet->data[1];
	debug_pair(vp);

	*tail = vp;
	tail = &(vp->next);

	vp = paircreate(PW_VQP_ERROR_CODE, PW_TYPE_OCTETS);
	if (!vp) {
		librad_log("No memory");
		return -1;
	}
	vp->lvalue = packet->data[2];
	debug_pair(vp);

	*tail = vp;
	tail = &(vp->next);

	vp = paircreate(PW_VQP_SEQUENCE_NUMBER, PW_TYPE_OCTETS);
	if (!vp) {
		librad_log("No memory");
		return -1;
	}
	vp->lvalue = packet->id; /* already set by vqp_recv */
	debug_pair(vp);

	*tail = vp;
	tail = &(vp->next);

	ptr = packet->data + VQP_HDR_LEN;
	end = packet->data + packet->data_len;

	/*
	 *	Note that vqp_recv() MUST ensure that the packet is
	 *	formatted in a way we expect, and that vqp_recv() MUST
	 *	be called before vqp_decode().
	 */
	while (ptr < end) {
		attribute = (ptr[2] << 8) | ptr[3];
		length = ptr[5];
		ptr += 6;

		/*
		 *	Hack to get the dictionaries to work correctly.
		 */
		attribute |= 0x2000;
		vp = paircreate(attribute, PW_TYPE_OCTETS);
		if (!vp) {
			pairfree(&packet->vps);

			librad_log("No memory");
			return -1;
		}

		switch (vp->type) {
		case PW_TYPE_IPADDR:
			if (length == 4) {
				memcpy(&vp->vp_ipaddr, ptr, 4);
				vp->length = 4;
				break;
			}
			vp->type = PW_TYPE_OCTETS;
			/* FALL-THROUGH */

		default:
		case PW_TYPE_STRING:
		case PW_TYPE_OCTETS:
			memcpy(vp->vp_octets, ptr, length);
			vp->length = length;
			break;
		}
		ptr += length;
		debug_pair(vp);

		*tail = vp;
		tail = &(vp->next);
	}

	/*
	 *	FIXME: Map attributes to Calling-Station-Id, etc...
	 */

	return 0;
}

/*
 *	These are the MUST HAVE contents for a VQP packet.
 *
 *	We don't allow the caller to give less than these, because
 *	it won't work.  We don't encode more than these, because the
 *	clients will ignore it.
 *
 *	FIXME: Be more generous?  Look for CISCO + VQP attributes?
 */
static int contents[5][VQP_MAX_ATTRIBUTES] = {
	{ 0,      0,      0,      0,      0,      0 },
	{ 0x0c01, 0x0c02, 0x0c03, 0x0c04, 0x0c07, 0x0c05 }, /* Join request */
	{ 0x0c03, 0x0c08, 0,      0,      0,      0 },	/* Join Response */
	{ 0x0c01, 0x0c02, 0x0c03, 0x0c04, 0x0c07, 0x0c08 }, /* Reconfirm */
	{ 0x0c03, 0x0c08, 0,      0,      0,      0 }
};

int vqp_encode(RADIUS_PACKET *packet, RADIUS_PACKET *original)
{
	int i, code, length;
	VALUE_PAIR *vp;
	uint8_t *ptr;
	VALUE_PAIR	*vps[VQP_MAX_ATTRIBUTES];

	if (!packet) {
		librad_log("Failed encoding VQP");
		return -1;
	}

	if (packet->data) return 0;

	vp = pairfind(packet->vps, PW_VQP_PACKET_TYPE);
	if (!vp) {
		librad_log("Failed to find VQP-Packet-Type in response packet");
		return -1;
	}

	code = vp->lvalue;
	if ((code < 1) || (code > 4)) {
		librad_log("Invalid value %d for VQP-Packet-Type", code);
		return -1;
	}

	length = VQP_HDR_LEN;
	memset(vps, 0, sizeof(vps));

	vp = pairfind(packet->vps, PW_VQP_ERROR_CODE);

	/*
	 *	FIXME: Map attributes from calling-station-Id, etc.
	 *
	 *	Maybe do this via rlm_vqp?  That's probably the
	 *	best place to add the code...
	 */

	/*
	 *	No error: encode attributes.
	 */
	if (!vp) for (i = 0; i < VQP_MAX_ATTRIBUTES; i++) {
		if (!contents[code][i]) break;

		vps[i] = pairfind(packet->vps, contents[code][i] | 0x2000);

		/*
		 *	FIXME: Print the name...
		 */
		if (!vps[i]) {
			librad_log("Failed to find VQP attribute %02x",
				   contents[code][i]);
			return -1;
		}

		length += 6;
		length += vps[i]->length;
	}

	packet->data = malloc(length);
	if (!packet->data) {
		librad_log("No memory");
		return -1;
	}
	packet->data_len = length;

	ptr = packet->data;

	ptr[0] = VQP_VERSION;
	ptr[1] = code;

	if (!vp) {
		ptr[2] = 0;
	} else {
		ptr[2] = vp->lvalue & 0xff;
		return 0;
	}

	/*
	 *	The number of attributes is hard-coded.
	 */
	if ((code == 1) || (code == 3)) {
		uint32_t sequence;

		ptr[3] = VQP_MAX_ATTRIBUTES;

		sequence = htonl(packet->id);
		memcpy(ptr + 4, &sequence, 4);
	} else {
		if (!original) {
			librad_log("Cannot send VQP response without request");
			return -1;
		}

		/*
		 *	Packet Sequence Number
		 */
		memcpy(ptr + 4, original->data + 4, 4);

		ptr[3] = 2;
	}

	ptr += 8;

	/*
	 *	Encode the VP's.
	 */
	for (i = 0; i < VQP_MAX_ATTRIBUTES; i++) {
		if (!vps[i]) break;
		vp = vps[i];

		debug_pair(vp);

		/*
		 *	Type.  Note that we look at only the lower 8
		 *	bits, as the upper 8 bits have been hacked.
		 *	See also dictionary.vqp
		 */
		ptr[0] = 0;
		ptr[1] = 0;
		ptr[2] = 0x0c;
		ptr[3] = vp->attribute & 0xff;

		/* Length */
		ptr[4] = 0;
		ptr[5] = vp->length & 0xff;

		ptr += 6;

		/* Data */
		switch (vp->type) {
		case PW_TYPE_IPADDR:
			memcpy(ptr, &vp->vp_ipaddr, 4);
			break;

		default:
		case PW_TYPE_OCTETS:
		case PW_TYPE_STRING:
			memcpy(ptr, vp->vp_octets, vp->length);
			break;
		}
		ptr += vp->length;
	}

	return 0;
}
#endif
