#ifdef __APPLE__
/*
 * Open Directory support from Apple Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 only, as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License version 2
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright 2007 Apple Inc.
 */

#include	<freeradius-devel/ident.h>
RCSID("$Id: opendir.c,v 1.8 2007/10/12 16:08:15 aland Exp $")

#include	<freeradius-devel/radiusd.h>
#include	<freeradius-devel/modules.h>
#include	<freeradius-devel/rad_assert.h>
#include        <freeradius-devel/md5.h>

#include 	<ctype.h>

#include	"smbdes.h"

#include <DirectoryService/DirectoryService.h>

static int getUserNodeRef(char* inUserName, char **outUserName,
			  tDirNodeReference* userNodeRef, tDirReference dsRef)
{
	tDataBuffer		*tDataBuff	= NULL;
	tDirNodeReference	nodeRef		= 0;
	long			status		= eDSNoErr;
	tContextData		context		= NULL;
	unsigned long		nodeCount	= 0;
	unsigned long		attrIndex	= 0;
	tDataList		*nodeName	= NULL;
	tAttributeEntryPtr	pAttrEntry	= NULL;
	tDataList		*pRecName	= NULL;
	tDataList		*pRecType	= NULL;
	tDataList		*pAttrType	= NULL;
	unsigned long		recCount	= 0;
	tRecordEntry		*pRecEntry	= NULL;
	tAttributeListRef	attrListRef	= 0;
	char			*pUserLocation	= NULL;
	tAttributeValueListRef	valueRef	= 0;
	tAttributeValueEntry	*pValueEntry	= NULL;
	tDataList		*pUserNode	= NULL;
	int			result		= RLM_MODULE_FAIL;
	
	if (inUserName == NULL) {
		radlog(L_ERR, "rlm_mschap: getUserNodeRef(): no username");
		return RLM_MODULE_FAIL;
	}
    
	tDataBuff = dsDataBufferAllocate(dsRef, 4096);
	if (tDataBuff == NULL) {
		radlog(L_ERR, "rlm_mschap: getUserNodeRef(): dsDataBufferAllocate() status = %ld", status);  
		return RLM_MODULE_FAIL;
	}
	
	do {
		/* find on search node */
		status = dsFindDirNodes(dsRef, tDataBuff, NULL,
					eDSAuthenticationSearchNodeName,
					&nodeCount, &context);
		if (status != eDSNoErr) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): no node found? status = %ld", status);  
			result = RLM_MODULE_FAIL;
			break;
		}
		if (nodeCount < 1) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): nodeCount < 1, status = %ld", status);  
			result = RLM_MODULE_FAIL;
			break;
		}
		
		status = dsGetDirNodeName(dsRef, tDataBuff, 1, &nodeName);
		if (status != eDSNoErr) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): dsGetDirNodeName() status = %ld", status);  
			result = RLM_MODULE_FAIL;
			break;
		}
		
		status = dsOpenDirNode(dsRef, nodeName, &nodeRef);
		dsDataListDeallocate(dsRef, nodeName);
		free(nodeName);
		nodeName = NULL;
		
		if (status != eDSNoErr) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): dsOpenDirNode() status = %ld", status);  
			result = RLM_MODULE_FAIL;
			break;
		}
		
		pRecName = dsBuildListFromStrings(dsRef, inUserName, NULL);
		pRecType = dsBuildListFromStrings(dsRef, kDSStdRecordTypeUsers,
						  NULL);
		pAttrType = dsBuildListFromStrings(dsRef,
						   kDSNAttrMetaNodeLocation,
						   kDSNAttrRecordName, NULL);
		
		recCount = 1;
		status = dsGetRecordList(nodeRef, tDataBuff, pRecName,
					 eDSExact, pRecType, pAttrType, 0,
					 &recCount, &context);
		if (status != eDSNoErr || recCount == 0) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): dsGetRecordList() status = %ld, recCount=%lu", status, recCount);  
			result = RLM_MODULE_FAIL;
			break;
		}
		
		status = dsGetRecordEntry(nodeRef, tDataBuff, 1,
					  &attrListRef, &pRecEntry);
		if (status != eDSNoErr) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): dsGetRecordEntry() status = %ld", status);  
			result = RLM_MODULE_FAIL;
			break;	
		}
		
		for (attrIndex = 1; (attrIndex <= pRecEntry->fRecordAttributeCount) && (status == eDSNoErr); attrIndex++) {
			status = dsGetAttributeEntry(nodeRef, tDataBuff, attrListRef, attrIndex, &valueRef, &pAttrEntry);
			if (status == eDSNoErr && pAttrEntry != NULL) {
				if (strcmp(pAttrEntry->fAttributeSignature.fBufferData, kDSNAttrMetaNodeLocation) == 0) {
					status = dsGetAttributeValue(nodeRef, tDataBuff, 1, valueRef, &pValueEntry);
					if (status == eDSNoErr && pValueEntry != NULL) {
						pUserLocation = (char *) calloc(pValueEntry->fAttributeValueData.fBufferLength + 1, sizeof(char));
						memcpy(pUserLocation, pValueEntry->fAttributeValueData.fBufferData, pValueEntry->fAttributeValueData.fBufferLength);
					}
				} else if (strcmp(pAttrEntry->fAttributeSignature.fBufferData, kDSNAttrRecordName) == 0) {
					status = dsGetAttributeValue(nodeRef, tDataBuff, 1, valueRef, &pValueEntry);
					if (status == eDSNoErr && pValueEntry != NULL) {
						*outUserName = (char *) malloc(pValueEntry->fAttributeValueData.fBufferLength + 1);
						bzero(*outUserName,pValueEntry->fAttributeValueData.fBufferLength + 1);
						memcpy(*outUserName, pValueEntry->fAttributeValueData.fBufferData, pValueEntry->fAttributeValueData.fBufferLength);
					}
				}
				
				if (pValueEntry != NULL) {
					dsDeallocAttributeValueEntry(dsRef, pValueEntry);
					pValueEntry = NULL;
				}
				
				dsDeallocAttributeEntry(dsRef, pAttrEntry);
				pAttrEntry = NULL;
				dsCloseAttributeValueList(valueRef);
				valueRef = 0;
			}
		}
		
		pUserNode = dsBuildFromPath(dsRef, pUserLocation, "/");
		if (pUserNode == NULL) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): dsBuildFromPath() returned NULL");  
			result = RLM_MODULE_FAIL;
			break;
		}
		
		status = dsOpenDirNode(dsRef, pUserNode, userNodeRef);
		dsDataListDeallocate(dsRef, pUserNode);
		free(pUserNode);

		if (status != eDSNoErr) {
			radlog(L_ERR,"rlm_mschap: getUserNodeRef(): dsOpenDirNode() status = %ld", status);  
			result = RLM_MODULE_FAIL;
			break;
		}
		
		result = RLM_MODULE_OK;
	}
	while (0);
	
	if (pRecEntry != NULL)
		dsDeallocRecordEntry(dsRef, pRecEntry);

	if (tDataBuff != NULL)
		dsDataBufferDeAllocate(dsRef, tDataBuff);
		
	if (pUserLocation != NULL)
		free(pUserLocation);
		
	if (pRecName != NULL) {
		dsDataListDeallocate(dsRef, pRecName);
		free(pRecName);
	}
	if (pRecType != NULL) {
		dsDataListDeallocate(dsRef, pRecType);
		free(pRecType);
	}
	if (pAttrType != NULL) {
		dsDataListDeallocate(dsRef, pAttrType);
		free(pAttrType);
	}
	if (nodeRef != 0)
		dsCloseDirNode(nodeRef);
	
	return  result;
}


int od_mschap_auth(REQUEST *request, VALUE_PAIR *challenge,
		   VALUE_PAIR * usernamepair)
{
	tDirStatus		status		 = eDSNoErr;
	tDirReference		dsRef		 = 0;
	tDirNodeReference	userNodeRef	 = 0;
	tDataBuffer		*tDataBuff	 = NULL;
	tDataBuffer		*pStepBuff	 = NULL;
	tDataNode		*pAuthType	 = NULL;
	unsigned long		uiCurr		 = 0;
	unsigned long		uiLen		 = 0;
	char			*username_string = NULL;
	char			*shortUserName	 = NULL;
	VALUE_PAIR		*response	 = pairfind(request->packet->vps, PW_MSCHAP2_RESPONSE);
#ifndef NDEBUG
	int t;
#endif
	
	username_string = (char *) malloc(usernamepair->length + 1);
	if (username_string == NULL)
		return RLM_MODULE_FAIL;
	
	strlcpy(username_string, (char *)usernamepair->vp_strvalue,
		usernamepair->length + 1);
	
	status = dsOpenDirService(&dsRef);
	if (status != eDSNoErr) {
		free(username_string);
		radlog(L_ERR,"rlm_mschap: od_mschap_auth(): dsOpenDirService = %d", status);
		return RLM_MODULE_FAIL;
	}
    
	status = getUserNodeRef(username_string, &shortUserName, &userNodeRef, dsRef);
	if(status != RLM_MODULE_OK) {
		DEBUG2("rlm_osx_od: ds_mschap_auth: getUserNodeRef failed");
		if (username_string != NULL)
			free(username_string);
		if (dsRef != 0)
			dsCloseDirService(dsRef);
		return status;
	}
	
	/* We got a node; fill the stepBuffer 
	   kDSStdAuthMSCHAP2
	   MS-CHAPv2 authentication method. The Open Directory plug-in generates the reply data for the client. 
	   The input buffer format consists of 
	   a four byte length specifying the length of the user name that follows, the user name, 
	   a four byte value specifying the length of the server challenge that follows, the server challenge, 
	   a four byte value specifying the length of the peer challenge that follows, the peer challenge, 
	   a four byte value specifying the length of the client's digest that follows, and the client's digest. 
	   The output buffer consists of a four byte value specifying the length of the return digest for the client's challenge.
	   r = FillAuthBuff(pAuthBuff, 5,
	   strlen(inName), inName,						// Directory Services long or short name
	   strlen(schal), schal,						// server challenge
	   strlen(peerchal), peerchal,					// client challenge
	   strlen(p24), p24,							// P24 NT-Response
	   4, "User");									// must match the username that was used for the hash
		
	   inName		= 	username_string
	   schal		=   challenge->vp_strvalue
	   peerchal	=   response->vp_strvalue + 2 (16 octets)
	   p24			=   response->vp_strvalue + 26 (24 octets)
	*/

	pStepBuff = dsDataBufferAllocate(dsRef, 4096);
	tDataBuff = dsDataBufferAllocate(dsRef, 4096);
	pAuthType = dsDataNodeAllocateString(dsRef, kDSStdAuthMSCHAP2);
	uiCurr = 0;
	
	DEBUG2("	rlm_mschap:username_string = %s, shortUserName=%s (length = %lu)\n", username_string, shortUserName, strlen(shortUserName));
	
	/* User name length + username */
	uiLen = strlen(shortUserName);
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &uiLen, sizeof(size_t));
	uiCurr += sizeof(size_t);
	memcpy(&(tDataBuff->fBufferData[uiCurr]), shortUserName, uiLen);
	uiCurr += uiLen;
#ifndef NDEBUG
	DEBUG2("	rlm_mschap: stepbuf server challenge:\t");
	for (t = 0; t < challenge->length; t++) {
		fprintf(stderr, "%02x", challenge->vp_strvalue[t]);
	}
	fprintf(stderr, "\n");
#endif
	
	/* server challenge (ie. my (freeRADIUS) challenge) */
	uiLen = 16;
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &uiLen, sizeof(size_t));
	uiCurr += sizeof(size_t);
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &(challenge->vp_strvalue[0]),
	       uiLen);
	uiCurr += uiLen;
	
#ifndef NDEBUG
	DEBUG2("	rlm_mschap: stepbuf peer challenge:\t\t");
	for (t = 2; t < 18; t++) {
		fprintf(stderr, "%02x", response->vp_strvalue[t]);
	}
	fprintf(stderr, "\n");
#endif
	
	/* peer challenge (ie. the client-generated response) */
	uiLen = 16;
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &uiLen, sizeof(size_t));
	uiCurr += sizeof(size_t);
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &(response->vp_strvalue[2]),
	       uiLen);
	uiCurr += uiLen;	
	
#ifndef NDEBUG
	DEBUG2("	rlm_mschap stepbuf p24:\t\t");
	for (t = 26; t < 50; t++) {
		fprintf(stderr, "%02x", response->vp_strvalue[t]);
	}
	fprintf(stderr, "\n");
#endif
	
	/* p24 (ie. second part of client-generated response) */
	uiLen =  24; /* strlen(&(response->vp_strvalue[26])); may contain NULL byte in the middle. */
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &uiLen, sizeof(size_t));
	uiCurr += sizeof(size_t);
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &(response->vp_strvalue[26]),
	       uiLen);
	uiCurr += uiLen;
	
	/* Client generated use name (short name?) */
	uiLen =  strlen(username_string);
	memcpy(&(tDataBuff->fBufferData[uiCurr]), &uiLen, sizeof(size_t));
	uiCurr += sizeof(size_t);
	memcpy(&(tDataBuff->fBufferData[uiCurr]), username_string, uiLen);
	uiCurr += uiLen;

	tDataBuff->fBufferLength = uiCurr;
	
	status = dsDoDirNodeAuth(userNodeRef, pAuthType, 1, tDataBuff,
				 pStepBuff, NULL);
	if (status == eDSNoErr) {
		if (pStepBuff->fBufferLength > 4) {
			unsigned long len;
			
			memcpy(&len, pStepBuff->fBufferData, 4);
			if (len == 40) {
				char mschap_reply[41] = { '\0' };
				pStepBuff->fBufferData[len+4] = '\0';
				mschap_reply[0] = 'S';
				mschap_reply[1] = '=';
				memcpy(&(mschap_reply[2]), &(pStepBuff->fBufferData[4]), len);
				mschap_add_reply(&request->reply->vps,
						 *response->vp_strvalue,
						 "MS-CHAP2-Success",
						 mschap_reply, len+2);
				DEBUG2("rlm_mschap: dsDoDirNodeAuth returns stepbuff: %s (len=%ld)\n", mschap_reply, len);
			}
		}
	}

	/* clean up */
	if (username_string != NULL)
		free(username_string);
	if (shortUserName != NULL)
		free(shortUserName);

	if (tDataBuff != NULL)
		dsDataBufferDeAllocate(dsRef, tDataBuff);
	if (pStepBuff != NULL)
		dsDataBufferDeAllocate(dsRef, pStepBuff);
	if (pAuthType != NULL)
		dsDataNodeDeAllocate(dsRef, pAuthType);
	if (userNodeRef != 0)
		dsCloseDirNode(userNodeRef);
	if (dsRef != 0)
		dsCloseDirService(dsRef);
	
	if (status != eDSNoErr) {
		errno = EACCES;
		radlog(L_ERR, "rlm_mschap: authentication failed %d", status); /* <-- returns -14091 (eDSAuthMethodNotSupported) -14090 */
		return RLM_MODULE_REJECT;
	}
	
	return RLM_MODULE_OK;
}

#endif /* __APPLE__ */
