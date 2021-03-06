// Original
#define failGracefully exit

// buffers for UDP and URIs
#define BUF_LEN 500
#define URI_BUF_LEN 32
#define MAXBUFLEN 100

#define ISSERVER 1
#define DEBUG 1

#ifdef __cplusplus
extern "C" {
#endif

#include "mainserver.h"
#include "lalarm.h"
#include "panautils.h"
#include "eax.h"


#ifdef __cplusplus
}
#endif

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>
#include "wpa_supplicant/src/common/defs.h"

//CantCoap
#include "cantcoap-master/cantcoap.h"
#include "wpa_supplicant/src/radius/radius.h"
#include "wpa_supplicant/src/radius/radius_client.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
/**
 * @file mainserver.c
 * @brief Functions to manage CoAP-EAP Controller.
 **/
/*
 *  Copyright (C) Dan Garcia Carrillo on 2021.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 */

//Global variables
static bool fin = 1;
int global_sockfd = 0;
int sockfd  = 0;

int successes = 0;

/** Linked list of server's CoAP-EAP sessions.*/
struct coap_eap_ctx_list* list_coap_eap_sessions = NULL;
struct coap_ctx_list* list_coap_ctx = NULL;

/** Mutex associated to CoAP-EAP sessions' list.*/
pthread_mutex_t list_sessions_mutex;

/** Linked list of server's tasks.*/
struct task_list* list_tasks = NULL;
/** Last task. */
struct task_list* last_task = NULL;
/** Mutex associated to tasks' list. */
pthread_mutex_t list_tasks_mutex;

/** Alarm's list. */
struct lalarm_coap* list_alarms_coap_eap = NULL;


char URI_PATH[50] ={0};
unsigned char cborCryptosuite[4] = {0x83,0x00,0x01,0x02};
unsigned char cborlifetime[4] = {0x81,0x19, 0x70,0x80};


/** Semaphore used to wait for new tasks by workers. */
sem_t got_task;



//                          |Code| Id |  LENGTH |Type|      Type-Data       c-->
//	uint8_t eap_req_id [50]={0x02,0xdf,0x00,0x0b,0x01,0x75,0x73,0x65,0x72,0x61,0x32};
uint8_t eap_req_id [50] = {0};




void printf_hex(unsigned char*, int);
void printf_hex(unsigned char* text, int length) {
    int i;
    for(i=0; i<length; i++)
        printf("%02x",text[i]);
    printf("\n");
    return;
}

int split (const char *str, char c, char ***arr)
{
    int count = 1;
    int token_len = 1;
    int i = 0;
    char *p;
    char *t;

    p = (char *)str;
    while (*p != '\0')
    {
        if (*p == c)
            count++;
        p++;
    }

    *arr = (char**) malloc(sizeof(char*) * count);
    if (*arr == NULL)
        exit(1);

    p = (char *)str;
    while (*p != '\0')
    {
        if (*p == c)
        {
            (*arr)[i] = (char*) malloc( sizeof(char) * token_len );
            if ((*arr)[i] == NULL)
                exit(1);

            token_len = 0;
            i++;
        }
        p++;
        token_len++;
    }
    (*arr)[i] = (char*) malloc( sizeof(char) * token_len );
    if ((*arr)[i] == NULL)
        exit(1);

    i = 0;
    p = (char *)str;
    t = ((*arr)[i]);
    while (*p != '\0')
    {
        if (*p != c && *p != '\0')
        {
            *t = *p;
            t++;
        }
        else
        {
            *t = '\0';
            i++;
            t = ((*arr)[i]);
        }
        p++;
    }

    return count;
}



//> get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

unsigned short get_in_port(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return (((struct sockaddr_in*)sa)->sin_port);
	}

	return (((struct sockaddr_in6*)sa)->sin6_port);
}


unsigned int print_ipv6_address(char *output, struct in6_addr *v6addr){
	char 				pv6addr[INET6_ADDRSTRLEN];

	if(inet_ntop(AF_INET6, v6addr, pv6addr, sizeof(pv6addr)) == NULL){
		puts("inet_ntop(): Error converting IPv6 Source Address to presentation format");
		return 0;
	}

	pana_debug(output, "coap://[%s]/auth", pv6addr);
	return 1;
}

struct network_task{
	char buf[BUF_LEN];
	int len;
	struct sockaddr_storage their_addr;	
	uint32_t session_id;
};



void printHexadecimal(CoapPDU *pdu){
#if DEBUG
    pana_debug("PDU: (%d)\n",pdu->getTokenPointer());
    printf_hex(pdu->getPDUPointer(),pdu->getPDULength());
#endif
}


network_task* createNetworkTask( char *buf, int len, struct sockaddr_storage *their_addr){

	network_task *task = XMALLOC(network_task,1);

	memcpy(&(task->buf), buf, (size_t)len);
	(task->buf)[len] = '\0';
	
	task->len = len;
    memcpy(&(task->their_addr), their_addr, sizeof(struct sockaddr_storage));

	return task;
}

void setSessionID(struct network_task *task, uint32_t session_id){
	task->session_id = session_id;
}


void signal_handler(int sig) {
	pana_debug("\nStopping server, signal: %d\n", sig);
	fin = 0;
}



void print_list_sessions(){
#if DEBUG
    struct coap_eap_ctx_list* ptr = list_coap_eap_sessions;
	// lock the mutex, to assure exclusive access to the list
	pthread_mutex_lock(&list_sessions_mutex);

	while (ptr != NULL) {
		pana_debug("Showing session id: %#X", ptr->coap_eap_session->session_id);
		ptr = ptr->next;
	}

	// unlock mutex
	pthread_mutex_unlock(&list_sessions_mutex);
#endif
}



void printDebug(coap_eap_ctx * coap_eap_session){
#if DEBUG
	CoapPDU *response = new CoapPDU (coap_eap_session->lastReceivedMessage, BUF_LEN, BUF_LEN);
	response->setPDULength(coap_eap_session->lastReceivedMessage_len);

	if(response->validate() != 1)
	{
		pana_debug("Error Parsing Message\n");
		exit(0);
	}

	struct timeval start;
	gettimeofday(&start, NULL);


	char s[INET6_ADDRSTRLEN];
	pana_debug("DEBUG:::::\n MSGID: %d IP: %s TimeOfDay %ld\n", ntohs(response->getMessageID()),
			inet_ntop(((coap_eap_session)->recvAddr).ss_family,
					get_in_addr((struct sockaddr *)&(coap_eap_session)->recvAddr),
					s, sizeof s),
			(start.tv_sec * 1000000 + start.tv_usec)
	);

#endif
}


// Task Functions
struct task_list* get_task() {
	int rc; /* return code of pthreads functions.  */

	struct task_list* task = NULL;


	pana_debug("Trying to get a task.");

	/* lock the mutex, to assure exclusive access to the list */
	rc = pthread_mutex_lock(&list_tasks_mutex);

	if (list_tasks != NULL) {
		task = list_tasks; 
		list_tasks = list_tasks->next;
		task->next = NULL;
	}


	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_tasks_mutex);

	/* return the task to the caller. */
	if (task == NULL) {

		pana_debug("Task not found");

		return NULL;
	}

	return task;
}

void
add_task(task_function funcion, void * arg) {
	
	if(arg == NULL)
	{
		pana_error("ERROR: add_task: arg  == NULL ");
		exit(0);	
	}
	
	int rc; // return code of pthreads functions.
	// lock the mutex, to assure exclusive access to the list
	rc = pthread_mutex_lock(&list_tasks_mutex);

	struct task_list * new_element; // A new element in the list

	// create structure with new element
	new_element = XMALLOC(struct task_list,1);

	new_element->use_function = funcion;
	new_element->data = arg;
	new_element->next = NULL;


	/* add new session to the end of the list, updating list */
	/* pointers as required */
	if (list_tasks == NULL) { /* special case - list is empty */
		list_tasks = new_element;
		last_task = new_element;
	}
	else {
		last_task->next = new_element;
		last_task = last_task->next;
	}


	pana_debug("add_task: added task");

	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_tasks_mutex);
	/* signal the condition variable - there's a new task to handle */
	rc = sem_post(&got_task);
}

// Hash functions

int
check_mac(unsigned char *key, unsigned char *mac, CoapPDU *pdu) {


	if(key == NULL || mac == NULL || pdu == NULL)
	{
			pana_error("ERROR: check_mac: ");
			if(key == NULL)
				pana_error("key == NULL ");
			if(mac == NULL)
				pana_error("mac == NULL ");
			if(pdu == NULL)
				pana_error("pdu == NULL ");

			if(pdu->getOptionPointer(CoapPDU::COAP_OPTION_AUTH) == NULL)
				pana_error("->getAUTHPointer() == NULL ");

		exit(0);	
	}

	unsigned char zero[16] = {0};
	unsigned char calculatedmac[16] = {0};

	memcpy(pdu->getOptionPointer(CoapPDU::COAP_OPTION_AUTH),&zero,16);


	int len = pdu->getPDULength();

	unsigned char *mypdu = (unsigned char *)malloc((pdu->getPDULength())*sizeof(unsigned char));
	memcpy(mypdu,pdu->getPDUPointer(),(size_t)len);

	do_omac(key,pdu->getPDUPointer(),pdu->getPDULength(), (unsigned char *)&calculatedmac);

	if (memcmp(mac,calculatedmac,16)==0) return 1;
	return 0;
}

void override_auth_option_pdu(unsigned char *key, CoapPDU *pdu){

	// Here we should generate OSCORE option

}

// Retransmissions funtions

void storeLastSentMessageInSession(CoapPDU *pdu, coap_eap_ctx *coap_eap_session){
	
	if(coap_eap_session->lastSentMessage != NULL)
	{
		free((void *) coap_eap_session->lastSentMessage);
		coap_eap_session->lastSentMessage = NULL;
	}

	coap_eap_session->lastSentMessage = XMALLOC(uint8_t,pdu->getPDULength());
	memcpy(coap_eap_session->lastSentMessage,pdu->getPDUPointer() ,(size_t)pdu->getPDULength());
	coap_eap_session->lastSentMessage_len = pdu->getPDULength();
}

void storeLastReceivedMessageInSession(CoapPDU *pdu, coap_eap_ctx *coap_eap_session){
	
	if(coap_eap_session->lastReceivedMessage != NULL)
	{
		free((void *) coap_eap_session->lastReceivedMessage);
		coap_eap_session->lastReceivedMessage = NULL;
	}


	pana_debug("Storing Last Received Message of length %d \n",pdu->getPDULength());

	coap_eap_session->lastReceivedMessage = XMALLOC(uint8_t,pdu->getPDULength());
	memcpy(coap_eap_session->lastReceivedMessage,pdu->getPDUPointer() ,(size_t)pdu->getPDULength());
	coap_eap_session->lastReceivedMessage_len = pdu->getPDULength();
	
}





void* process_receive_radius_msg(void* arg) {

    if(arg == NULL)
    {
        printf("ERROR: process_receive_radius_msg: arg  == NULL \n");
        exit(0);
    }

    printf("\n??\n"
			   "##\n"
				"######## ENTER: process_receive_radius_msg \n");


    struct radius_func_parameter radius_params = *((struct radius_func_parameter*) arg);
    int radius_type = RADIUS_AUTH;

    //Get the function's parameters.
    struct radius_ms_radiug *radmsg = (struct radius_ms_radiug *)radius_params.msg;

    // Get the information about the new message received
    struct radius_client_data *radius_data = get_rad_client_ctx();
    struct radius_hdr *hdr = radius_msg_get_hdr((struct radius_msg *)radmsg);
    struct eap_auth_ctx *eap_ctx = search_eap_ctx_rad_client(hdr->identifier);

    if (eap_ctx == NULL){
        printf("eap_ctx NULL. It can't be used\n");
        return NULL;
    }

    coap_eap_ctx * coap_eap_session = (coap_eap_ctx*) (eap_ctx->eap_ll_ctx);
    pthread_mutex_lock(&(coap_eap_session->mutex));

#if DEBUG
    printDebug(coap_eap_session);
#endif

    radius_client_receive((struct radius_msg *)radmsg, radius_data, &radius_type);

    // In case of a EAP Fail is produced.
    if ((eap_auth_get_eapFail(eap_ctx) == TRUE)){
        printf("Error: There's an eap fail in RADIUS\n");
        exit(0);
    }



    if ((eap_auth_get_eapReq(eap_ctx) == TRUE) || (eap_auth_get_eapSuccess(eap_ctx) == TRUE)) {


        printf("There's an eap request in RADIUS\n");
		printf("Trying to make a transition with the message from RADIUS\n");

        struct wpabuf * packet = eap_auth_get_eapReqData(&(coap_eap_session->eap_ctx));


        socklen_t addrLen = sizeof(struct sockaddr_in);
        if((&coap_eap_session->recvAddr)->ss_family==AF_INET6) {
            addrLen = sizeof(struct sockaddr_in6);
        }




        if(coap_eap_session->eap_workarround == 0){

            get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
            coap_eap_session->eap_workarround++;
            mempcpy(eap_req_id, wpabuf_head(packet), wpabuf_len(packet));

            // TODO:
            os_memcpy(coap_eap_session->userID, "alpha.t.eu.org", os_strlen("alpha.t.eu.org"));

            // Copiamos el ID
            eap_req_id[0] = 0x02;
            eap_req_id[3] = 5+strlen(coap_eap_session->userID);
            memset(&eap_req_id[5],0,45);
            mempcpy(&eap_req_id[5], coap_eap_session->userID, 5+strlen(coap_eap_session->userID));

            eap_auth_set_eapResp(&(coap_eap_session->eap_ctx), TRUE);
            eap_auth_set_eapRespData(&(coap_eap_session->eap_ctx), eap_req_id, 5+strlen(coap_eap_session->userID));
            eap_auth_step(&(coap_eap_session->eap_ctx));

            pthread_mutex_unlock(&(coap_eap_session->mutex));

            return;
        }





        coap_eap_session->message_id += 1;

        printf("Creating CoAP PDU after RADIUS exchange\n");

        // FIXME: Orden de creaci??n, secuencia, y location path din??mico
        CoapPDU *response = new CoapPDU();
        response->setVersion(1);
        response->setMessageID(coap_eap_session->message_id);
        response->setToken((uint8_t *)&coap_eap_session->session_id,4);
        response->setCode(CoapPDU::COAP_POST);
        response->setType(CoapPDU::COAP_CONFIRMABLE);

		printf("The Stored URI is %s \n",coap_eap_session->location);
		response->setURI(coap_eap_session->location, strlen(coap_eap_session->location));




        if (eap_auth_get_eapKeyAvailable(&(coap_eap_session->eap_ctx)))
        {
            size_t key_len = 0;
            u8 *key = eap_auth_get_eapKeyData(&(coap_eap_session->eap_ctx),(size_t *)&(coap_eap_session->key_len));

            coap_eap_session->msk_key = XMALLOC(u8 , (coap_eap_session->key_len) );

            memcpy(coap_eap_session->msk_key,key,coap_eap_session->key_len);
          // Here we would verify the OSCORE Option


            coap_eap_session->CURRENT_STATE = 3;

            printf("Cambio a estado 3 message_id: %d, session_id: %X\n",coap_eap_session->message_id,coap_eap_session->session_id);

        }

        else{
		pana_debug("message_id: %d\n",coap_eap_session->message_id);
		pana_debug("NO Cambio a estado 3 message_id: %d, session_id: %X\n",coap_eap_session->message_id,coap_eap_session->session_id);
	}

	
	if (eap_auth_get_eapSuccess(eap_ctx) == TRUE) {			
		printf("EAP SUCCESS::::::::::::::::::::::\n");		
	}
	


	if (eap_auth_get_eapSuccess(eap_ctx) == TRUE) 
	{			


		unsigned char tempPayload[100]={0};
		unsigned char oscore_option[2] = {0x09,0x00};

		response->addOption(CoapPDU::COAP_OPTION_OSCORE,2,oscore_option);

		unsigned char payloadOSCORE [] = {0xad, 0x88, 0x3c, 0x74, 0x28, 0x13, 0x2e, 0x52, 0xbe, 0x82, 0x57, 0x65, 0xb2, 0x61, 0x86, 0xf5, 0x35, 0x84, 0x82, 0x49, 0xa7, 0x45};
      
  /*      
	if (eap_auth_get_eapSuccess(eap_ctx) != TRUE) {			
				printf("The Stored URI is %s \n",coap_eap_session->location);
				response->setURI(coap_eap_session->location, strlen(coap_eap_session->location));
	}
*/
      response->setPayload(
				payloadOSCORE,
				22
				);

	}else{


	if (eap_auth_get_eapSuccess(eap_ctx) != TRUE) {			
				//response->setURI(coap_eap_session->location, strlen(coap_eap_session->location));
//				response->setURI((char*)"/.well-known/a",14);
	}

	        response->setPayload(
					(unsigned char *)wpabuf_head(packet),
					(int)wpabuf_len(packet)
					);
	}

        if ((coap_eap_session->msk_key != NULL) && (coap_eap_session->auth_key != NULL))
            override_auth_option_pdu(coap_eap_session->auth_key,response);


        if((coap_eap_session->msk_key != NULL))
			{
				pana_debug("MSK KEY:\n");
				printf_hex(coap_eap_session->msk_key,16);

			}



			// Send new coap Message
	//#if DEBUG			
			printf("PDU TO SEND: \n");
			printHexadecimal(response);
	//#endif
	
		printf("Sending message in RADIUS response \n");
			response->printHuman();
    
        ssize_t sent = sendto(
                global_sockfd,
                response->getPDUPointer(),
                (size_t)response->getPDULength(),
                0,
                (sockaddr *)&coap_eap_session->recvAddr,
                addrLen
        );
        if(sent<0) {
            DBG("Error sending packet: %ld.",sent);
            perror(NULL);
            return NULL;
        }




        storeLastSentMessageInSession(response,coap_eap_session);

        get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
        coap_eap_session->RT = coap_eap_session->RT_INIT;
        coap_eap_session->RTX_COUNTER = 0;
        add_alarm_coap_eap(&(list_alarms_coap_eap),coap_eap_session,coap_eap_session->RT,POST_ALARM);

        char s[INET6_ADDRSTRLEN];
						pana_debug("Alarma a??andida:::::::\n  MSGID: %d IP: %s\n", ntohs(response->getMessageID()),
								inet_ntop(((coap_eap_session)->recvAddr).ss_family,
										get_in_addr((struct sockaddr *)&(coap_eap_session)->recvAddr),
										s, sizeof s)
						);


    }

#if DEBUG
    printDebug(coap_eap_session);
#endif

    pthread_mutex_unlock(&(coap_eap_session->mutex));


    pana_debug("######## SALIMOS DE : process_receive_radius_msg \n"
			"##\n"
			"??\n"
	);

    //free(&radius_params);
    return NULL;

}






void coapRetransmitLastSentMessage(coap_eap_ctx * coap_eap_session){

	if(coap_eap_session == NULL)
	{
		pana_error("ERROR: coapRetransmit: coap_eap_session  == NULL ");
		exit(0);	
	}


	pana_debug(	"\n??\n"
			    "##\n"
				"######## ENTRAMOS EN: coapRetransmitLastSentMessage \n");
#if DEBUG
	printDebug(coap_eap_session);
#endif

	CoapPDU *response = new CoapPDU(coap_eap_session->lastSentMessage, BUF_LEN, BUF_LEN);
	response->setPDULength(coap_eap_session->lastSentMessage_len);

	if(response->validate() != 1){
			pana_debug("Malformed CoapPDU \n");
			exit(0);
	}
	
	//response->printHuman();

	//struct sockaddr_storage * recvFrom = coap_eap_session->recvAddr;
	int sockfd = global_sockfd;
	
	socklen_t addrLen = sizeof(struct sockaddr_in);
	if( (&coap_eap_session->recvAddr)->ss_family==AF_INET6) {
		addrLen = sizeof(struct sockaddr_in6);
	}


	coap_eap_session->RT=(coap_eap_session->RT*2);
	get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
	add_alarm_coap_eap(&(list_alarms_coap_eap),coap_eap_session,coap_eap_session->RT,POST_ALARM);

	ssize_t sent = sendto(
			sockfd,
			response->getPDUPointer(),
            (size_t) response->getPDULength(),
			0,
			(sockaddr *)&coap_eap_session->recvAddr,
			addrLen
			);
	if(sent<0) {
		DBG("Error sending packet: %ld.",sent);
		perror(NULL);

	}


	delete response;


#if DEBUG
	printDebug(coap_eap_session);
#endif
	pana_debug("######## SALIMOS DE: coapRetransmitLastSentMessage \n"
			"##\n"
			"??\n"
	);

	
}


void remove_coap_eap_session(uint32_t id) {
	int rc;

	struct coap_eap_ctx_list* session = NULL;
	struct coap_eap_ctx_list* anterior = NULL;

	pana_debug("Trying to delete session with id: %d", ntohl(id));
	// lock the mutex, to assure exclusive access to the list 
	rc = pthread_mutex_lock(&list_sessions_mutex);

	if (list_coap_eap_sessions != NULL) {
		session = list_coap_eap_sessions;
		//If the session is the first
		if (session->coap_eap_session->session_id == id) {
			pana_debug("Found and deleted session with id: %d", ntohl(id));
			list_coap_eap_sessions = list_coap_eap_sessions->next;
			session->next=NULL;
			//XFREE(session); //fixme: Cuidado al poner este free. Hay que verlo con el de remove_alarm (lalarm.c)
			rc = pthread_mutex_unlock(&list_sessions_mutex);
			return;
		}
		session = list_coap_eap_sessions->next;
		anterior = list_coap_eap_sessions;
		while (session != NULL) {
			if (session->coap_eap_session->session_id == id) {
				anterior->next = session->next;
				session->next = NULL;
				//XFREE(session); //fixme: Cuidado al poner este free. Hay que verlo con el de remove_alarm (lalarm.c)
				break;
			}
			anterior = anterior->next;
			session = session->next;
		}
	}

	// unlock mutex 
	rc = pthread_mutex_unlock(&list_sessions_mutex);

}



void* process_retr_coap_eap(void *arg){

	if(arg == NULL)
	{
		pana_error("ERROR: process_retr_coap_eap: arg  == NULL ");
		exit(0);	
	}


	pana_debug(	"\n??\n"
			"##\n"
			"######## ENTRAMOS EN: process_retr_coap_eap \n");


	struct retr_coap_func_parameter* retr_params;


	// Get the function's parameters.
	retr_params = (struct retr_coap_func_parameter*) arg;
	int alarm_id = retr_params->id;
	coap_eap_ctx * coap_eap_session = retr_params->session;
	pthread_mutex_lock(&(coap_eap_session->mutex));



#if DEBUG
	printDebug(coap_eap_session);
#endif

	// Depends on the alarm produced, it is processed.
	if (alarm_id == POST_ALARM) {

		
		pana_debug("Processing POST_AUTH alarm ocurred %d\n", coap_eap_session->session_id);
		

		//get_alarm_coap_eap_session(coap_eap_session->list_of_alarms, coap_eap_session->session_id, POST_ALARM);
		get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
		coap_eap_session->RTX_COUNTER++;
		if (coap_eap_session->RTX_COUNTER < MAX_RETRANSMIT) {

			if (coap_eap_session->lastSentMessage != NULL)
			{

				pana_debug("Retransmiting %f\n",coap_eap_session->RT);

				coapRetransmitLastSentMessage(coap_eap_session);
            }
			/*else
				pana_debug("No message for retransmitting\n");
			*/
		}
		else {
			pana_debug("Timeout %d\n",coap_eap_session->session_id);
			int session = coap_eap_session->session_id;
			remove_coap_eap_session(session);
			remove_coap_eap_session(session);
			//XFREE(coap_eap_session); //Rafa: Watch out!!!
		}
		
	} 
	else if (alarm_id == SESS_ALARM)
	{
		pana_debug("Session expired\n");
	}	
	else {
		pana_debug("An UNKNOWN alarm ocurred");
	}




#if DEBUG
	printDebug(coap_eap_session);
#endif


	pthread_mutex_unlock(&(coap_eap_session->mutex));


	pana_debug("######## SALIMOS DE: process_retr_coap_eap \n"
			"##\n"
			"??\n"
	);

	return NULL;
}

// Coap EAP Session Functions
void add_coap_eap_session(coap_eap_ctx * session) {
	
	if(session == NULL)
	{
		pana_error("ERROR: add_coap_eap_session: session  == NULL ");
		exit(0);	
	}
	
	int rc; //> return code of pthreads functions.  

	struct coap_eap_ctx_list * new_element;

	/* create structure with new request */
	new_element = XMALLOC(struct coap_eap_ctx_list,1);
	new_element->coap_eap_session = session;
	new_element->next = NULL;

	/* lock the mutex, to assure exclusive access to the list */
	rc = pthread_mutex_lock(&list_sessions_mutex);

	/* add new session to the end of the list, updating list */
	/* pointers as required */
	if (list_coap_eap_sessions == NULL) { /* special case - list is empty */
		list_coap_eap_sessions = new_element;
	} 
	else {
		new_element->next = list_coap_eap_sessions;
		list_coap_eap_sessions = new_element;
	}

	pana_debug("add_session: added CoAP EAP session: %X",ntohl(new_element->coap_eap_session->session_id));
	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_sessions_mutex);

}

coap_eap_ctx* get_coap_eap_session(uint32_t id) {
	int rc; /* return code of pthreads functions.  */

	struct coap_eap_ctx_list* session = NULL;

//	pana_debug("Trying to get session of id: %X", htonl(id));
	/* lock the mutex, to assure exclusive access to the list */
	rc = pthread_mutex_lock(&list_sessions_mutex);

	if (list_coap_eap_sessions != NULL) {
		session = list_coap_eap_sessions;
		while (session != NULL) {
			if (session->coap_eap_session->session_id == id) break;
			session = session->next;
		}
	}

	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_sessions_mutex);

	/* return the session to the caller. */
	if (session == NULL) {
		pana_debug("Session not found, id: %d", ntohl(id));
		exit(0);	
	}
	return session->coap_eap_session;
}


// Handler Funtions

void * handle_worker(void* data) {
	
	if(data == NULL)
	{
		pana_error("ERROR: handle_worker: data  == NULL ");
		exit(0);	
	}


	pana_debug(	"\n??\n"
			"##\n"
			"######## ENTRAMOS EN: handle_worker\n");


	int thread_id = *((int*) data); /* thread identifying number */
	int rc; /* return code of pthreads functions.  */
	struct task_list* a_task = NULL; /* pointer to a task. */


	pana_debug("thread '%d' as worker manager", thread_id);
	pana_debug("Starting thread '%d'", thread_id);


	/* lock the mutex, to access the requests list exclusively. */
	sem_wait(&got_task);

	/* do forever.... */
	while (fin) {


		pana_debug("thread '%d' tries to get a task", thread_id);


		if (list_tasks != NULL) { /* a request is pending */
			a_task = get_task();
		}

		if (a_task) {
			//pana_debug("Hay task!");
			a_task->use_function(a_task->data);
		}
		rc = sem_wait(&got_task);
		/* and after we return from pthread_cond_wait, the mutex  */
		/* is locked again, so we don't need to lock it ourselves */
	}


		pana_debug("######## SALIMOS DE: handle_worker\n"
		"##"
		"??");


	return NULL;
}


void* process_coap_msg(void *arg){
	if(arg == NULL)
	{
		pana_error("ERROR: process_coap_msg: arg  == NULL ");
		exit(0);	
	}


	printf("@"
			"##"
			"######## ENTRAMOS EN: process_coap_msg\n");


	struct network_task *mytask = (struct network_task *)arg;

	CoapPDU *request = new CoapPDU((uint8_t *)mytask->buf, BUF_LEN, BUF_LEN);
	request->setPDULength(mytask->len);

	if(request->validate() != 1){
		pana_debug("Malformed CoapPDU \n");
		exit(0);
	}


#if DEBUG

#endif
	request->printHuman();

	struct sockaddr_storage * recvFrom = &(mytask->their_addr);
	int sockfd = global_sockfd;
	uint32_t session_id = mytask->session_id;
	
	
	coap_eap_ctx *coap_eap_session = NULL;


	pana_debug("Session ID: %x \n", session_id);
	pana_debug("Session ID2: %x \n", session_id);


	coap_eap_session = get_coap_eap_session(session_id);

	if(coap_eap_session == NULL )
	{	
		pana_debug("Error getting coap_eap_session\n");
		exit(0);		
	}
	
	int rc =  pthread_mutex_lock(&(coap_eap_session->mutex));
	struct wpabuf * packet;

	pana_debug("######## IN PROCESS RECEVIE COAP\n");

#if DEBUG
	printDebug(coap_eap_session);
#endif

	socklen_t addrLen = sizeof(struct sockaddr_in);
	if(recvFrom->ss_family==AF_INET6) {
		addrLen = sizeof(struct sockaddr_in6);
	}


	//  prepare next message, a POST
	CoapPDU *pdu = new CoapPDU();
	pdu->setVersion(1);
	pdu->setType(CoapPDU::COAP_CONFIRMABLE);
	pdu->setCode(CoapPDU::COAP_POST);
	pdu->setToken((uint8_t*)&coap_eap_session->session_id,sizeof(uint32_t));
	pdu->setMessageID(coap_eap_session->message_id );

	printf("---------------------------- \n");
	printf("Payload of the first message %d \n",request->getPayloadLength());
	printf("---------------------------- \n");

	if(request->getPayloadLength() == 0){	
			coap_eap_session->location = strdup("/.well-known/a");
		//		pdu->setURI((char*)"/.well-known/a",14);
	}else{
			coap_eap_session->location = (char *) malloc((request->getPayloadLength() + 5 )* sizeof(char));
			memset(coap_eap_session->location, 0, (request->getPayloadLength() + 5 )* sizeof(char));
			memcpy(coap_eap_session->location,request->getPayloadPointer(),request->getPayloadLength());
			//pdu->setURI((char*) request->getPayloadPointer(),request->getPayloadLength());					
	}

			printf("The Stored URI is %s \n",coap_eap_session->location);
			pdu->setURI((char*) coap_eap_session->location,strlen(coap_eap_session->location));


	// Empezamos con el tratamiento EAP, enviamos el primer put
	eap_auth_set_eapRestart(&(coap_eap_session->eap_ctx), TRUE);
	eap_auth_step(&(coap_eap_session->eap_ctx));
	packet = eap_auth_get_eapReqData(&(coap_eap_session->eap_ctx));

	unsigned char tempPayload[100]={0};
	memcpy(tempPayload, (uint8_t *)wpabuf_head(packet),(uint8_t) wpabuf_len(packet));
	memcpy(tempPayload+(uint8_t) wpabuf_len(packet),cborCryptosuite,4);

	//pdu->setPayload((uint8_t *)wpabuf_head(packet),(uint8_t) wpabuf_len(packet));
	pdu->setPayload((uint8_t *)tempPayload,(uint8_t) wpabuf_len(packet)+4 );



	coap_eap_session->CURRENT_STATE = 2;

	storeLastReceivedMessageInSession(request,coap_eap_session);
	storeLastSentMessageInSession(pdu,coap_eap_session);




	get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
	coap_eap_session->RTX_COUNTER = 0;
	coap_eap_session->RT = coap_eap_session->RT_INIT;
	add_alarm_coap_eap(&(list_alarms_coap_eap),coap_eap_session,coap_eap_session->RT,POST_ALARM);

	
	char s[INET6_ADDRSTRLEN];
	pana_debug("Alarma a??andida:::::::\n  MSGID: %d IP: %s\n", ntohs(pdu->getMessageID()),
			inet_ntop(((coap_eap_session)->recvAddr).ss_family,
					get_in_addr((struct sockaddr *)&(coap_eap_session)->recvAddr),
					s, sizeof s)
	);
	
	pana_debug("SENDING POST\n");
#if DEBUG
    printHexadecimal(pdu);
#endif
    pdu->printHuman();

    ssize_t sent2 = sendto(
			sockfd,
			pdu->getPDUPointer(),
            (size_t) pdu->getPDULength(),
			0,
			(sockaddr*)recvFrom,
			addrLen
		     );
	if(sent2<0) {
		DBG("Error sending packet: %ld.",sent);
		perror(NULL);
		//return NULL;
	}


	delete request;
	//delete response;
	delete pdu;
	delete mytask;

#if DEBUG
	printDebug(coap_eap_session);
#endif

	rc =  pthread_mutex_unlock(&(coap_eap_session->mutex));


    pana_debug("######## SALIMOS DE: process_coap_msg\n"
			"##\n"
			"??\n"
	);

	return 0;

}


void* process_acknowledgment(void * arg){

	if(arg == NULL)
	{
		pana_error("ERROR: process_acknowledgment: arg  == NULL ");
		exit(0);	
	}



	pana_debug(	"\n??\n"
			"##\n"
			"######## ENTRAMOS EN: process_acknowledgment\n");


	struct network_task *mytask = (struct network_task *)arg;

	CoapPDU *request = new CoapPDU((uint8_t *)mytask->buf, BUF_LEN, BUF_LEN);
	request->setPDULength(mytask->len);
	
	if(request->validate() != 1){
			pana_debug("Error: PDU invalido \n");
			exit(0);
	}

	
	struct sockaddr_storage * recvFrom = &mytask->their_addr;
	int sockfd = global_sockfd;

	coap_eap_ctx *coap_eap_session=NULL;
	uint32_t session_id;
	memcpy(&session_id, request->getTokenPointer(), (size_t) request->getTokenLength());
	coap_eap_session = get_coap_eap_session(session_id);
	
	
	if(coap_eap_session == NULL )
	{	
		pana_debug("Error getting coap_eap_session\n");
		exit(0);		
	}
	
	int rc = pthread_mutex_lock(&(coap_eap_session->mutex));


	pana_debug("######## PROCESSING... ACKNOWLEDGMENT\n");


#if DEBUG
	printDebug(coap_eap_session);
#endif

	socklen_t addrLen = sizeof(struct sockaddr_in);
	if(recvFrom->ss_family==AF_INET6) {
		addrLen = sizeof(struct sockaddr_in6);
	}

	char URI[30] = {0};
	int URI_len;
    char ** split_uri = NULL;
    int split_uri_len;

	CoapPDU *response = new CoapPDU();

	struct wpabuf * packet;
    unsigned char *lpath = NULL;
	ssize_t sent;
	uint8_t *dst;

	bool auth_option_present=FALSE, finish = FALSE;
	unsigned char mac[16] = {0};

	int session;

			unsigned char tempEAPPayload[100] = {0};

	printf("CURRENT_STATE %d\n",coap_eap_session->CURRENT_STATE);


	//char responseURI[10] = {0};
	if(coap_eap_session->CURRENT_STATE == 1) {
	/*	
		coap_eap_session->location = (char *) malloc(10 * sizeof(char));
		memset(coap_eap_session->location, 0, 10);
	*/
	}


	int lengthEAP = 0;

			free(coap_eap_session->location);
			
			memset(URI,0,30);
			request->getLocation(URI,30,&URI_len);
			printf("The Retrieved URI is %s , len %d\n",URI,URI_len);
			coap_eap_session->location = strdup(URI);
			printf("The Stored URI is %s \n",coap_eap_session->location);

			pana_debug("==============");
			pana_debug("\nURI PATH(%d): %s \n",URI_len, coap_eap_session->location);
			pana_debug("==============");



	switch(coap_eap_session->CURRENT_STATE) {

		

		case 2:
			
			printf("Entramos en el estado 2\n");
			

			if(request->getOptionPointer(CoapPDU::COAP_OPTION_AUTH) != NULL){
				pana_debug("Error: estado 2, auth option presente \n");
				exit(0);
			}


			// We need to look for the  cyphersuite

			memcpy(tempEAPPayload,request->getPayloadPointer(), 
                    (size_t)request->getPayloadLength());
	
			printf("EAP_tempPayload: \n");
		  printf_hex(tempEAPPayload, (size_t)request->getPayloadLength());

			lengthEAP = (tempEAPPayload[2]*10)+tempEAPPayload[3];
			printf("Length EAP 0: %d\n",tempEAPPayload[2]);
			printf("Length EAP 1: %d\n",tempEAPPayload[3]);
			printf("Length EAP: %d\n", lengthEAP);
			
			printf("Length EAP vs Payload: %d -- %d\n",lengthEAP,(size_t)request->getPayloadLength());
			
		  if((size_t)request->getPayloadLength()>lengthEAP ){
			printf("CBOR content\n");
		 printf_hex( tempEAPPayload+(lengthEAP),
 	               (size_t)request->getPayloadLength()-lengthEAP
 								);
	
			}

			eap_auth_set_eapResp(&(coap_eap_session->eap_ctx), TRUE);
			eap_auth_set_eapRespData(
										&(coap_eap_session->eap_ctx), 
											request->getPayloadPointer(), 
											lengthEAP
									);
	
  			eap_auth_step(&(coap_eap_session->eap_ctx));




			break;

		case 3:
			
			pana_debug("Entramos en el estado 3\n");
			pana_debug("Final Binding.Checking AUTH OPTION\n");
			

			auth_option_present = TRUE;

			dst = request->getPDUPointer();

			get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);


			if (auth_option_present)
			{ 	

				int session = coap_eap_session->session_id;
				get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
				remove_coap_eap_session(session);
				remove_coap_eap_session(session);

			}else {
				pana_debug("AUTH option is not present!!!!\n");
				int session = coap_eap_session->session_id;
				get_alarm_coap_eap_session(coap_eap_session->list_of_alarms, coap_eap_session->session_id, POST_ALARM);
				remove_coap_eap_session(session);				
				remove_coap_eap_session(session);

			}
			break;

		default:
			break;

	}
	
	delete request;
	delete response;



	rc = pthread_mutex_unlock(&(coap_eap_session->mutex));

	pana_debug("######## SALIMOS DE: process_acknowledgment\n"
			"##\n"
			"??\n"
	);


}

int get_coap_address(	void *their_addr, unsigned short port) {
    int rc; /* return code of pthreads functions.  */

    struct coap_eap_ctx_list* session = NULL;
    socklen_t addrLen = sizeof(struct sockaddr_in);

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(&list_sessions_mutex);

    if (list_coap_eap_sessions != NULL) {
        session = list_coap_eap_sessions;
        while (session != NULL) {
            
	    pana_debug("Checking address: ");
            printf_hex((unsigned char *)their_addr, (size_t)addrLen);
			pana_debug("with port: %d\n", port);

            pana_debug("Checking ...: ");
            printf_hex((unsigned char *)
                    get_in_addr((struct sockaddr *) &(session->coap_eap_session)->recvAddr), (size_t)addrLen);
			unsigned short  portChecking = get_in_port((struct sockaddr *) &(session->coap_eap_session)->recvAddr);
			pana_debug("with port: %d\n", portChecking);

			if (memcmp(
					get_in_addr((struct sockaddr *) &(session->coap_eap_session)->recvAddr)
					, their_addr, (size_t) addrLen) == 0){

				if(port == portChecking ){
					pana_debug("Found!\n");
					break;
				}
			}
            session = session->next;
        }
    }

    /* unlock mutex */
    rc = pthread_mutex_unlock(&list_sessions_mutex);

    /* return the session to the caller. */
    if (session == NULL) {
        pana_debug("Address not found");
        return FALSE;
    }
    return TRUE;
}




void * handle_network_management(void *data) {

#define MYPORT "5683"

	// the port users will be connecting to
	coap_eap_ctx *new_coap_eap_session = NULL;

	//To handle exit signals
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);

	fd_set mreadset; // master read set

	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	int ret;

	struct sockaddr_storage their_addr;
	char buf[BUF_LEN];
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];


	char uriBuffer[URI_BUF_LEN];
	int recvURILen = 0;

	char * ipaddr = "aaaa::1";

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	// if ((rv = getaddrinfo(ipaddr, MYPORT, &hints, &servinfo)) != 0) {
	if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0) {

	#ifdef DEBUG
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	#endif
		return NULL;
	}
	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((global_sockfd = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}
		if (bind(global_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(global_sockfd);
			perror("listener: bind");
			continue;
		}
		break; 
	}
	if (p == NULL) {
	#ifdef DEBUG
		fprintf(stderr, "listener: failed to bind socket\n");
	#endif
		return NULL;
	}

	// socket para enviar

	memset(&hints, 0, sizeof hints);
	hints.ai_family 		= AF_INET6;
	hints.ai_socktype 		= SOCK_DGRAM;

	if ((rv = getaddrinfo(NULL, "8000", &hints, &servinfo)) != 0) {

	#ifdef DEBUG
		pana_debug(stderr, "getaddrinfo: %s\n",gai_strerror(rv));
  #endif
		return NULL;
	}

	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
		{
			perror("talker: socket");
			continue;
		}
		break; 
	}

	if (p == NULL) {
	#ifdef DEBUG
		fprintf(stderr, "talker: failed to bind socket\n");
	#endif
		return NULL;
	}



	// Radius
	int radius_sock=0; //Init it to a non-valid value

	struct sockaddr_in sa;
	struct sockaddr_in6 sa6;

	rad_client_init(AS_IP, AS_PORT, AS_SECRET);

	struct radius_client_data *radius_data = get_rad_client_ctx();

	if (radius_data != NULL) {
		if (IP_VERSION_AUTH==4)
			radius_sock = radius_data->auth_serv_sock;
		else if (IP_VERSION_AUTH==6)
			radius_sock = radius_data->auth_serv_sock6;
	}

	u8 udp_packet[MAX_DATA_LEN];
    struct sockaddr_in eap_ll_dst_addr, radius_dst_addr;
	struct sockaddr_in6 eap_ll_dst_addr6, radius_dst_addr6; //For ipv6 support
	int addr_size;

	//struct pana_func_parameter *pana_params;
	struct radius_func_parameter *radius_params;
	pana *msg;
	int length;



	while(fin){

		FD_ZERO(&mreadset);
		FD_SET(global_sockfd, &mreadset);
		FD_SET(radius_sock, &mreadset);
		
		// -- 
		sigset_t emptyset, blockset;

        sigemptyset(&blockset);         /* Block SIGINT */
        sigaddset(&blockset, SIGINT);
        sigprocmask(SIG_BLOCK, &blockset, NULL);

        /* Initialize nfds and readfds, and perhaps do other work here */
        /* Unblock signal, then wait for signal or ready file descriptor */

        sigemptyset(&emptyset);
        int retSelect  = pselect(FD_SETSIZE, &mreadset, NULL, NULL, NULL, &emptyset);

		//int retSelect = select(FD_SETSIZE,&mreadset,NULL,NULL,NULL);

		if(retSelect>0){


			if (FD_ISSET(radius_sock, &mreadset)) 
			{

				pana_debug( "\n??\n"
						"##\n"
					"######## MENSAJE RADIUS RECIBIDO\n");


                length = 0;

				if (IP_VERSION==4){
					addr_size = sizeof (radius_dst_addr);
					length = (int) recvfrom(radius_sock, udp_packet, sizeof (udp_packet), 0, (struct sockaddr *) &(radius_dst_addr), (socklen_t *)&(addr_size));
				}
				else if (IP_VERSION==6){
					addr_size = sizeof (radius_dst_addr6);
					length = (int) recvfrom(radius_sock, udp_packet, sizeof (udp_packet), 0, (struct sockaddr *) &(radius_dst_addr6), (socklen_t *)&(addr_size));
				}
				if (length > 0) 
				{

          radius_params = XMALLOC(struct radius_func_parameter,1);

					struct radius_msg *radmsg = radius_msg_parse(udp_packet, (size_t)length);
					radius_params->msg = (struct radius_msg *)XMALLOC ( char,length);
					memcpy(radius_params->msg, radmsg, (size_t)length);
					add_task(process_receive_radius_msg, radius_params);                    

				}
				else
					pana_error("recvfrom returned ret=%d, errno=%d", length, errno);


				pana_debug("######## FIN PROCESAMIENTO MENSAJE RADIUS\n"
						"##\n"
						"??\n"
				);

			}




			// CoAP Traffic
			if(FD_ISSET(global_sockfd,&mreadset)){



				CoapPDU *recvPDU = new CoapPDU((uint8_t*)buf,BUF_LEN,BUF_LEN);

				addr_len = sizeof their_addr;
				if ((numbytes = (int) recvfrom(global_sockfd, buf, MAXBUFLEN-1 , 0,
								(struct sockaddr *)&their_addr, &addr_len)) == -1) {
					perror("recvfrom");
					exit(1);
				}


				pana_debug(	"\n??\n"
						"##\n"
						"######## MENSAJE RECIBIDO\n");
				pana_debug("listener: got packet from %s\n",
						inet_ntop(their_addr.ss_family,
							get_in_addr((struct sockaddr *)&their_addr),
							s, sizeof s));
				pana_debug("port: %d\n", get_in_port((struct sockaddr *)&their_addr) );



				pana_debug("listener: packet is %d bytes long\n", numbytes);

				buf[numbytes] = '\0';

				pana_debug("listener: packet contains \"%s\"\n", buf);


				// validate packet
				if(numbytes>BUF_LEN) {
					INFO("PDU too large to fit in pre-allocated buffer");
					continue;
				}
				
				recvPDU->setPDULength(numbytes);
				if(recvPDU->validate()!=1) {
					INFO("Malformed CoAP packet");
					continue;
				}


#if DEBUG
        printHexadecimal(recvPDU);
#endif
	recvPDU->printHuman();

				network_task * new_task  = createNetworkTask(buf, numbytes, &their_addr);


				if(recvPDU->getType() != CoapPDU::COAP_ACKNOWLEDGEMENT) {

					pana_debug("######## GET RECIBIDO\n");


					pana_debug("SESSION NOT FOUND, CREATE A NEW ONE\n");


					new_coap_eap_session = XMALLOC(coap_eap_ctx,1);
					init_CoAP_EAP_Session(new_coap_eap_session);
					
					int rc = pthread_mutex_lock(&(new_coap_eap_session->mutex));
						
						memcpy(&new_coap_eap_session->recvAddr, &their_addr, sizeof(struct sockaddr_storage));
						pana_debug("The new session_id is %X\n", new_coap_eap_session->session_id);

						setSessionID(new_task,new_coap_eap_session->session_id);
						
						new_coap_eap_session->list_of_alarms=&(list_alarms_coap_eap);
						add_coap_eap_session(new_coap_eap_session);	
						
						storeLastReceivedMessageInSession(recvPDU,new_coap_eap_session);

					rc = pthread_mutex_unlock(&(new_coap_eap_session->mutex));
	
					add_task(process_coap_msg, new_task);                    	


				} else if(recvPDU->getType() == CoapPDU::COAP_ACKNOWLEDGEMENT){

					pana_debug("######## ACK RECIBIDO\n");


					// Nos aseguramos de que el mensaje no es un duplicado
					uint32_t session_id;
					memcpy(&session_id, recvPDU->getTokenPointer(), (size_t)recvPDU->getTokenLength());

					coap_eap_ctx * coap_eap_session = get_coap_eap_session(session_id);

					if(coap_eap_session == NULL )
					{
						pana_debug("Error getting coap_eap_session\n");
						exit(0);
					}

					int rc = pthread_mutex_lock(&(coap_eap_session->mutex));


					// Vemos que el ultimo mensaje recivido no sea el mismo que el actual
					CoapPDU *lastReceived =  new CoapPDU((uint8_t*)coap_eap_session->lastReceivedMessage,BUF_LEN,BUF_LEN);
					lastReceived->setPDULength(coap_eap_session->lastReceivedMessage_len);

					CoapPDU *lastSent =  new CoapPDU((uint8_t*)coap_eap_session->lastSentMessage,BUF_LEN,BUF_LEN);
					lastSent->setPDULength(coap_eap_session->lastSentMessage_len);


					if(lastReceived->validate() != 1 || lastSent->validate() != 1){
						INFO(" Malformed CoAP packet");
						rc = pthread_mutex_unlock(&(coap_eap_session->mutex));
						continue;
					}

					if(ntohs(recvPDU->getMessageID()) != ntohs(lastSent->getMessageID()) )
					{

						pana_debug("DUPLICADO: Mensaje fuera de orden");
					}

					else{
						storeLastReceivedMessageInSession(recvPDU,coap_eap_session);
						add_task(process_acknowledgment, new_task);                    
					}
				
					rc = pthread_mutex_unlock(&(coap_eap_session->mutex));
					delete lastReceived;
				}


				// code==0, no payload, this is a ping request, send RST
				if(recvPDU->getPDULength()==0&&recvPDU->getCode()==0) {
					INFO("CoAP ping request");
				}else
				{
					delete recvPDU;
				}


				pana_debug("######## FIN PROCESAMIENTO DE MENSAJE RECIBIDO\n"
						"##\n"
						"??\n"
				);


			}	
		}
	}

	close(global_sockfd);
	return NULL;
}


void * handle_alarm_coap_management(void *data) {


    pana_debug("Enter handle_alarm_coap_management\n");


    while (TRUE){ // Do it while the PAA is activated.

		struct retr_coap_func_parameter retrans_params;
		// Get the actual timestamp.
		double time = getTime();

		struct lalarm_coap* alarm = NULL;
		while ((alarm=get_next_alarm_coap_eap(&list_alarms_coap_eap, time)) != NULL)
        {
            pana_debug("Looking for alarms\n");

            retrans_params.session = alarm->coap_eap_session;
			retrans_params.id = 0;
			
			if (alarm->id == POST_ALARM) 
			{

				pana_debug("A POST_AUTH alarm ocurred %d\n",retrans_params.session->session_id);

				retrans_params.id = POST_ALARM;
				add_task(process_retr_coap_eap, &retrans_params);     
			}

			else { // An unknown alarm is activated.
				pana_debug("\nAn UNKNOWN alarm ocurred\n");
			}

		}
		waitusec(TIME_WAKE_UP);
	}
	return NULL;
}

//>
//> MAIN
//>


int main(int argc, char* argv[]) {

	// Variables needed to use threads
	int num_threads = NUM_WORKERS +1; // Workers & network manager
	int i; //loop counter
	int thr_id[num_threads]; // thread IDs
	pthread_t p_threads[num_threads]; // thread's structures

	load_config_server();


    pana_debug("\n Server operation mode:");

    if(MODE)
        pana_debug("PASSTHROUGH\n\n");
    else
        pana_debug("STANDALONE\n\n");


	global_sockfd = socket(AF_INET6, SOCK_DGRAM, 0);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	sem_init(&got_task, 0, 0);
#pragma clang diagnostic pop

	//Init the lockers
	pthread_mutex_init(&list_sessions_mutex, NULL);
	pthread_mutex_init(&list_tasks_mutex, NULL);

	//Init global variables
	list_alarms_coap_eap = init_alarms_coap();

	for (i = 0; i < NUM_WORKERS; i++) {
		thr_id[i] = i;
		pthread_create(&p_threads[i], NULL, handle_worker, (void*) &thr_id[i]);
		pthread_detach(p_threads[i]);
	}

    //Create alarm manager thread (void *(*)(void *))
    i+=1;
    thr_id[i] = i;
    pthread_create(&p_threads[i], NULL, handle_alarm_coap_management, NULL);

	//Once the workers are executed, the network manager function starts
	handle_network_management(NULL);
	pana_debug("OpenPANA-CoAP: The server has stopped.\n");
	return 0;
}



