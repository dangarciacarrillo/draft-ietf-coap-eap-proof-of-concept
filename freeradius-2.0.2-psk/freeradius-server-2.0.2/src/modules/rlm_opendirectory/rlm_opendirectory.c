/*
 * rlm_opendirectory.c
 *		authentication: Apple Open Directory authentication
 *		authorization:  enforces ACLs
 *
 * Version:	$Id: rlm_opendirectory.c,v 1.4 2008/01/04 09:46:57 aland Exp $
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

/*
 * 	For a typical Makefile, add linker flag like this:
 *	LDFLAGS = -framework DirectoryService
 */

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <DirectoryService/DirectoryService.h>
#include <membership.h>

#if HAVE_APPLE_SPI
#include <membershipPriv.h>
#else
int mbr_check_service_membership(const uuid_t user, const char *servicename, int *ismember);
int mbr_check_membership_refresh(const uuid_t user, uuid_t group, int *ismember);
#endif

/* RADIUS service ACL constants */
#define kRadiusSACLName		"com.apple.access_radius"
#define kRadiusServiceName	"radius"

/*
 *	od_check_passwd
 *
 *  Returns: ds err
 */

static long od_check_passwd(const char *uname, const char *password)
{
	long						result				= eDSAuthFailed;
	tDirReference				dsRef				= 0;
    tDataBuffer				   *tDataBuff			= NULL;
    tDirNodeReference			nodeRef				= 0;
    long						status				= eDSNoErr;
    tContextData				context				= NULL;
	unsigned long				nodeCount			= 0;
	unsigned long				attrIndex			= 0;
	tDataList				   *nodeName			= NULL;
    tAttributeEntryPtr			pAttrEntry			= NULL;
	tDataList				   *pRecName			= NULL;
	tDataList				   *pRecType			= NULL;
	tDataList				   *pAttrType			= NULL;
	unsigned long				recCount			= 0;
	tRecordEntry		  	 	*pRecEntry			= NULL;
	tAttributeListRef			attrListRef			= 0;
	char					   *pUserLocation		= NULL;
	char					   *pUserName			= NULL;
	tAttributeValueListRef		valueRef			= 0;
	tAttributeValueEntry  	 	*pValueEntry		= NULL;
	tDataList				   *pUserNode			= NULL;
	tDirNodeReference			userNodeRef			= 0;
	tDataBuffer					*pStepBuff			= NULL;
	tDataNode				   *pAuthType			= NULL;
	tAttributeValueEntry	   *pRecordType			= NULL;
	unsigned long				uiCurr				= 0;
	unsigned long				uiLen				= 0;
	unsigned long				pwLen				= 0;
	
	if (uname == NULL || password == NULL)
		return result;
	
	do
	{		
		status = dsOpenDirService( &dsRef );
		if ( status != eDSNoErr )
			return result;
		
		tDataBuff = dsDataBufferAllocate( dsRef, 4096 );
		if (tDataBuff == NULL)
			break;
		
		/* find user on search node */
		status = dsFindDirNodes( dsRef, tDataBuff, NULL, eDSSearchNodeName, &nodeCount, &context );
		if (status != eDSNoErr || nodeCount < 1)
			break;
		
		status = dsGetDirNodeName( dsRef, tDataBuff, 1, &nodeName );
		if (status != eDSNoErr)
			break;
		
		status = dsOpenDirNode( dsRef, nodeName, &nodeRef );
		dsDataListDeallocate( dsRef, nodeName );
		free( nodeName );
		nodeName = NULL;
		if (status != eDSNoErr)
			break;

		pRecName = dsBuildListFromStrings( dsRef, uname, NULL );
		pRecType = dsBuildListFromStrings( dsRef, kDSStdRecordTypeUsers, kDSStdRecordTypeComputers, kDSStdRecordTypeMachines, NULL );
		pAttrType = dsBuildListFromStrings( dsRef, kDSNAttrMetaNodeLocation, kDSNAttrRecordName, kDSNAttrRecordType, NULL );
		
		recCount = 1;
		status = dsGetRecordList( nodeRef, tDataBuff, pRecName, eDSExact, pRecType,
													pAttrType, 0, &recCount, &context );
		if ( status != eDSNoErr || recCount == 0 )
			break;
				
		status = dsGetRecordEntry( nodeRef, tDataBuff, 1, &attrListRef, &pRecEntry );
		if ( status != eDSNoErr )
			break;
		
		for ( attrIndex = 1; (attrIndex <= pRecEntry->fRecordAttributeCount) && (status == eDSNoErr); attrIndex++ )
		{
			status = dsGetAttributeEntry( nodeRef, tDataBuff, attrListRef, attrIndex, &valueRef, &pAttrEntry );
			if ( status == eDSNoErr && pAttrEntry != NULL )
			{
				if ( strcmp( pAttrEntry->fAttributeSignature.fBufferData, kDSNAttrMetaNodeLocation ) == 0 )
				{
					status = dsGetAttributeValue( nodeRef, tDataBuff, 1, valueRef, &pValueEntry );
					if ( status == eDSNoErr && pValueEntry != NULL )
					{
						pUserLocation = (char *) calloc( pValueEntry->fAttributeValueData.fBufferLength + 1, sizeof(char) );
						memcpy( pUserLocation, pValueEntry->fAttributeValueData.fBufferData, pValueEntry->fAttributeValueData.fBufferLength );
					}
				}
				else
				if ( strcmp( pAttrEntry->fAttributeSignature.fBufferData, kDSNAttrRecordName ) == 0 )
				{
					status = dsGetAttributeValue( nodeRef, tDataBuff, 1, valueRef, &pValueEntry );
					if ( status == eDSNoErr && pValueEntry != NULL )
					{
						pUserName = (char *) calloc( pValueEntry->fAttributeValueData.fBufferLength + 1, sizeof(char) );
						memcpy( pUserName, pValueEntry->fAttributeValueData.fBufferData, pValueEntry->fAttributeValueData.fBufferLength );
					}
				}
				else
				if ( strcmp( pAttrEntry->fAttributeSignature.fBufferData, kDSNAttrRecordType ) == 0 )
				{
					status = dsGetAttributeValue( nodeRef, tDataBuff, 1, valueRef, &pValueEntry );
					if ( status == eDSNoErr && pValueEntry != NULL )
					{
						pRecordType = pValueEntry;
						pValueEntry = NULL;
					}
				}
				
				if ( pValueEntry != NULL ) {
					dsDeallocAttributeValueEntry( dsRef, pValueEntry );
					pValueEntry = NULL;
				}
				if ( pAttrEntry != NULL ) {
					dsDeallocAttributeEntry( dsRef, pAttrEntry );
					pAttrEntry = NULL;
				}
				dsCloseAttributeValueList( valueRef );
				valueRef = 0;
			}
		}
		
		pUserNode = dsBuildFromPath( dsRef, pUserLocation, "/" );
		status = dsOpenDirNode( dsRef, pUserNode, &userNodeRef );
		dsDataListDeallocate( dsRef, pUserNode );
		free( pUserNode );
		pUserNode = NULL;
		if ( status != eDSNoErr )
			break;
		
		pStepBuff = dsDataBufferAllocate( dsRef, 128 );
		
		pAuthType = dsDataNodeAllocateString( dsRef, kDSStdAuthNodeNativeClearTextOK );
		uiCurr = 0;
		
		/* User name */
		uiLen = strlen( pUserName );
		memcpy( &(tDataBuff->fBufferData[ uiCurr ]), &uiLen, sizeof(unsigned long) );
		uiCurr += sizeof( unsigned long );
		memcpy( &(tDataBuff->fBufferData[ uiCurr ]), pUserName, uiLen );
		uiCurr += uiLen;
		
		/* pw */
		pwLen = strlen( password );
		memcpy( &(tDataBuff->fBufferData[ uiCurr ]), &pwLen, sizeof(unsigned long) );
		uiCurr += sizeof( unsigned long );
		memcpy( &(tDataBuff->fBufferData[ uiCurr ]), password, pwLen );
		uiCurr += pwLen;
		
		tDataBuff->fBufferLength = uiCurr;
		
		result = dsDoDirNodeAuthOnRecordType( userNodeRef, pAuthType, 1, tDataBuff, pStepBuff, NULL, &pRecordType->fAttributeValueData );
	}
	while ( 0 );
	
	/* clean up */
	if (pAuthType != NULL) {
		dsDataNodeDeAllocate( dsRef, pAuthType );
		pAuthType = NULL;
	}
	if (pRecordType != NULL) {
		dsDeallocAttributeValueEntry( dsRef, pRecordType );
		pRecordType = NULL;
	}
	if (tDataBuff != NULL) {
		bzero( tDataBuff, tDataBuff->fBufferSize );
		dsDataBufferDeAllocate( dsRef, tDataBuff );
		tDataBuff = NULL;
	}
	if (pStepBuff != NULL) {
		dsDataBufferDeAllocate( dsRef, pStepBuff );
		pStepBuff = NULL;
	}
	if (pUserLocation != NULL) {
		free(pUserLocation);
		pUserLocation = NULL;
	}
	if (pRecName != NULL) {
		dsDataListDeallocate( dsRef, pRecName );
		free( pRecName );
		pRecName = NULL;
	}
	if (pRecType != NULL) {
		dsDataListDeallocate( dsRef, pRecType );
		free( pRecType );
		pRecType = NULL;
	}
	if (pAttrType != NULL) {
		dsDataListDeallocate( dsRef, pAttrType );
		free( pAttrType );
		pAttrType = NULL;
	}
	if (nodeRef != 0) {
		dsCloseDirNode(nodeRef);
		nodeRef = 0;
	}
	if (dsRef != 0) {
		dsCloseDirService(dsRef);
		dsRef = 0;
	}
	
	return result;
}


/*
 *	Check the users password against the standard UNIX
 *	password table.
 */
int od_authenticate(void *instance, REQUEST *request)
{
	char *name, *passwd;
	int		ret;
	long odResult = eDSAuthFailed;
	
	/*
	 *	We can only authenticate user requests which HAVE
	 *	a User-Name attribute.
	 */
	if (!request->username) {
		radlog(L_AUTH, "rlm_opendirectory: Attribute \"User-Name\" is required for authentication.");
		return RLM_MODULE_INVALID;
	}

	/*
	 *	If the User-Password attribute is absent, is it MS-CHAPv2?
	 */
	if (!request->password) {
		radlog(L_AUTH, "rlm_opendirectory: Attribute \"User-Password\" is required for authentication.");
		return RLM_MODULE_INVALID;
	}
	
	/*
	 *  Ensure that we're being passed a plain-text password,
	 *  and not anything else.
	 */
	if (request->password->attribute != PW_PASSWORD) {
		radlog(L_AUTH, "rlm_opendirectory: Attribute \"User-Password\" is required for authentication.  Cannot use \"%s\".",
				request->password->name);
		return RLM_MODULE_INVALID;
	}
	
	name = (char *)request->username->vp_strvalue;
	passwd = (char *)request->password->vp_strvalue;
	
	odResult = od_check_passwd(name, passwd);
	switch(odResult)
	{
		case eDSNoErr:
			ret = RLM_MODULE_OK;
			break;
			
		case eDSAuthUnknownUser:
		case eDSAuthInvalidUserName:
		case eDSAuthNewPasswordRequired:
		case eDSAuthPasswordExpired:
		case eDSAuthAccountDisabled:
		case eDSAuthAccountExpired:
		case eDSAuthAccountInactive:
		case eDSAuthInvalidLogonHours:
		case eDSAuthInvalidComputer:
			ret = RLM_MODULE_USERLOCK;
			break;
		
		default:
			ret = RLM_MODULE_REJECT;
			break;
	}
	
	if (ret != RLM_MODULE_OK) {
		radlog(L_AUTH, "rlm_opendirectory: [%s]: invalid password", name);
		return ret;
	}
		
	return RLM_MODULE_OK;
}


/*
 *	member of the radius group?
 */
int od_authorize(void *instance, REQUEST *request)
{
	char *name = NULL;
	struct passwd *userdata = NULL;
	struct group *groupdata = NULL;
	int ismember = 0;
	RADCLIENT *rad_client = NULL;
	uuid_t uuid;
	uuid_t guid_sacl;
	uuid_t guid_nasgroup;
	int err;
	char host_ipaddr[128] = {0};
	
	if (request == NULL || request->username == NULL) {
		radlog(L_AUTH, "rlm_opendirectory: Attribute \"User-Name\" is required for authorization.");
		return RLM_MODULE_INVALID;
	}
	
	/* resolve SACL */
	uuid_clear(guid_sacl);
	groupdata = getgrnam(kRadiusSACLName);
	if (groupdata != NULL) {
		err = mbr_gid_to_uuid(groupdata->gr_gid, guid_sacl);
		if (err != 0) {
			radlog(L_ERR, "rlm_opendirectory: The group \"%s\" does not have a GUID.", kRadiusSACLName);
			return RLM_MODULE_FAIL;
		}		
	}
	else {
		radlog(L_DBG, "rlm_opendirectory: The SACL group \"%s\" does not exist on this system.", kRadiusSACLName);
	}
	
	/* resolve client access list */
	uuid_clear(guid_nasgroup);

	rad_client = request->client;
#if 0
	if (rad_client->community[0] != '\0' )
	{
		/*
		 *	The "community" can be a GUID (Globally Unique ID) or
		 *	a group name
		 */
		if (uuid_parse(rad_client->community, guid_nasgroup) != 0) {
			/* attempt to resolve the name */
			groupdata = getgrnam(rad_client->community);
			if (groupdata == NULL) {
				radlog(L_AUTH, "rlm_opendirectory: The group \"%s\" does not exist on this system.", rad_client->community);
				return RLM_MODULE_FAIL;
			}
			err = mbr_gid_to_uuid(groupdata->gr_gid, guid_nasgroup);
			if (err != 0) {
				radlog(L_AUTH, "rlm_opendirectory: The group \"%s\" does not have a GUID.", rad_client->community);
				return RLM_MODULE_FAIL;
			}
		}
	}
	else
#endif
	{
		if (rad_client == NULL) {
			radlog(L_DBG, "rlm_opendirectory: The client record could not be found for host %s.",
					ip_ntoh(&request->packet->src_ipaddr,
						host_ipaddr, sizeof(host_ipaddr)));
		}
		else {
			radlog(L_DBG, "rlm_opendirectory: The host %s does not have an access group.",
					ip_ntoh(&request->packet->src_ipaddr,
						host_ipaddr, sizeof(host_ipaddr)));
		}
	}
	
	if (uuid_is_null(guid_sacl) && uuid_is_null(guid_nasgroup)) {
		radlog(L_DBG, "rlm_opendirectory: no access control groups, all users allowed.");
		return RLM_MODULE_OK;
	}

	/* resolve user */
	uuid_clear(uuid);
	name = (char *)request->username->vp_strvalue;
	if (name != NULL) {
		userdata = getpwnam(name);
		if (userdata != NULL) {
			err = mbr_uid_to_uuid(userdata->pw_uid, uuid);
			if (err != 0)
				uuid_clear(uuid);
		}
	}
	
	if (uuid_is_null(uuid)) {
		radlog(L_AUTH, "rlm_opendirectory: Could not get the user's uuid.");
		return RLM_MODULE_NOTFOUND;
	}
	
	if (!uuid_is_null(guid_sacl)) {
		err = mbr_check_service_membership(uuid, kRadiusServiceName, &ismember);
		if (err != 0) {
			radlog(L_AUTH, "rlm_opendirectory: Failed to check group membership.");
			return RLM_MODULE_FAIL;
		}
		
		if (ismember == 0) {
			radlog(L_AUTH, "rlm_opendirectory: User <%s> is not authorized.", name ? name : "unknown");
			return RLM_MODULE_USERLOCK;
		}
	}
	
	if (!uuid_is_null(guid_nasgroup)) {
		err = mbr_check_membership_refresh(uuid, guid_nasgroup, &ismember);
		if (err != 0) {
			radlog(L_AUTH, "rlm_opendirectory: Failed to check group membership.");
			return RLM_MODULE_FAIL;
		}
		
		if (ismember == 0) {
			radlog(L_AUTH, "rlm_opendirectory: User <%s> is not authorized.", name ? name : "unknown");
			return RLM_MODULE_USERLOCK;
		}
	}
	
	radlog(L_AUTH, "rlm_opendirectory: User <%s> is authorized.", name ? name : "unknown");
	return RLM_MODULE_OK;
}


/* globally exported name */
module_t rlm_opendirectory = {
	RLM_MODULE_INIT,
	"opendirectory",
	RLM_TYPE_THREAD_SAFE,	/* type */
	NULL,			/* instantiation */
	NULL,               	/* detach */
	{
		od_authenticate, /* authentication */
		od_authorize,	/* authorization */
		NULL,		/* preaccounting */
		NULL,		/* accounting */
		NULL,		/* checksimul */
		NULL,		/* pre-proxy */
		NULL,		/* post-proxy */
		NULL		/* post-auth */
	},
};
