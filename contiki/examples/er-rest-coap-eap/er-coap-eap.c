/*
 * Copyright (c) 2022, JULIAN NIKLAS SCHIMMELPFENNIG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/**
 * \file
 *      Erbium (Er) CoAP-EAP IoT device
 * \author
 *      JULIAN NIKLAS SCHIMMELPFENNIG
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contiki.h"
#include "contiki-net.h"

#include "eap-peer.h"

/* Initial resource /hateoas_initial_resource. When called, it will be "overwritten" and a new random Resource will be created. */
#define REST_RES_HATEOAS 1

// Usage of ntohs for the EAP response.
#define ntohs(n) (((((unsigned short)(n) & 0xFF)) << 8) | (((unsigned short)(n) & 0xFF00) >> 8))

// function for debugging the output of the EAP state machine
void printf_hex(unsigned char*, int);
void printf_hex(unsigned char* text, int length) {
    printf("\n");
    int i;
    for(i=0; i<length; i++)
        printf("%02x",text[i]);
    printf("\n");
    return;
}

int cipherSuitesSent;
uint8_t eapKeyAvailable;

int counterCryptoSuite = 1;
int counterEapResponse = 1;
int hateoas_handler_counter = 1;

/*
  IP addresses for sending the inital POST request
*/

// Examples:
// #define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xfe80, 0, 0, 0, 0x0212, 0x7402, 0x0002, 0x0202) /* cooja2 */
// #define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0x0001) /* cooja2 */

// IP address of the er-example-server mote (the request is sent to the same mote)
// #define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xaaaa, 0, 0, 0, 0x0212, 0x7402, 0x0002, 0x0202) /* cooja2 */

// IP address for the coap-controller outside of Cooja (see tunslip tunnel)
#define SERVER_NODE(ipaddr)   uip_ip6addr(ipaddr, 0xaaaa, 0, 0, 0, 0, 0x00ff, 0xfe00, 0x0001) /* cooja2 */
#define REMOTE_PORT     UIP_HTONS(COAP_DEFAULT_PORT)

uip_ipaddr_t server_ipaddr;
static struct etimer et;

#include "er-coap-13.h"
#include "er-coap-13-engine.h"

/******************************************************************************/

#include "erbium.h"

/* For CoAP-specific example: not required for normal RESTful Web service. */
#if WITH_COAP == 3
#include "er-coap-03.h"
#elif WITH_COAP == 7
#include "er-coap-07.h"
#elif WITH_COAP == 12
#include "er-coap-12.h"
#elif WITH_COAP == 13
#include "er-coap-13.h"
#else
#warning "Erbium example without CoAP-specifc functionality"
#endif /* CoAP-specific example */

#define DEBUG 1
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]", (lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3], (lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

/******************************************************************************/

#if REST_RES_HATEOAS
/* 
  The size of the char array can't be modified later. 
  urlString[] will be always (24+1)*sizeof(char) bytes large, if 
  char urlString[] = "hateoas_initial_resource";
  sizeof(urlString));

  The following line will result in 404 not found!
  char urlString[] = "/.well-known/a";
  It has to be 
  char urlString[] = ".well-known/a";
*/
// The following values for the char array urlString[] cause problems with the implementation of the CoAP EAP controller.
// char urlString[] = "hateoas_initial_resource";

// Sub-URI work as well
// char urlString[] = ".well-known/a";

char urlString[] = "initHateoasRsc";
int urlStringCounter;

  // Skipping first handler call due to retransmission error on coapeapcontroller
int skipFirstHandlerCall = 0;

RESOURCE(hateoas, METHOD_POST, urlString, "title=\"HATEOAS dynamic resource\";rt=\"Debug\"");

void hateoas_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
  printf("\n");
  printf("hateoas_handler_counter = %d\n", hateoas_handler_counter);
  hateoas_handler_counter++;
  /*
  The following will not compile because of the following error: "for loop initial declarations are only allowed in C99 mode. Use option -std=c99 or -std=gnu99 to compile your code"
  for(int i = 0; i < 5; i++) {
    urlString[i] = 'A' + (random_rand() % 26);
  }
  */

  // Skipping first handler call due to retransmission error on coapeapcontroller
  if(skipFirstHandlerCall == 1) {
    for(urlStringCounter = 0; urlStringCounter < 5; urlStringCounter++) {
      // rand typically returns a 16-bit number
      urlString[urlStringCounter] = 'A' + (random_rand() % 26);
    }
    // terminating the string after 5 random uppercase characters
    urlString[5] = '\0';
  }

  skipFirstHandlerCall = 1;

  // Following line not needed, because the chars are already altered!
  // resource_hateoas.url = urlString;

  const uint8_t *payloadData = NULL;
  int payloadLength = REST.get_request_payload(request, &payloadData);
  printf("Payload received from the POST request: ");
  printf_hex(payloadData, payloadLength);

  // variable eapKeyAvailable can not be renamed due to by RFC 4137
  if(!eapKeyAvailable) {
        // passing the payload received in the request to the eap_peer_sm_step, see apps\eap-sm\eap-peer.c
        // eapReq from eap-peer.c as well! NECESSARY!!!
        eapReq = TRUE;
        eap_peer_sm_step(payloadData);
        uint16_t len = ntohs( ((struct eap_msg*) eapRespData)->length);
        // eap state machines reponse is accessible in eapRespData variable.
        
        if(cipherSuitesSent == 1) {
              // return early, because cipherSuites will only sent once
              // preventing a lot of jumps.

              // see erbium.h in struct rest_implementation_status for the codes
              REST.set_response_status(response, REST.status.CREATED);
              REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
              REST.set_header_location(response, resource_hateoas.url);

              // 3rd parameter in set_reponse_payload is size_t length. Is the datatype size_t is unsigned integral type. It represents the size of any object in bytes and returned by sizeof operator. It is used for array indexing and counting. It can never be negative. The return type of strcspn, strlen functions is size_t.
              REST.set_response_payload(response, eapRespData, len);

              printf("counterEapResponse = %d\n", counterEapResponse);
              counterEapResponse++;

              printf("eapResponse Data: ");
              printf_hex(eapRespData, len);
        }

        else {
              // sending the ciphersuites to the coap controller, only done once!

              char tempPayload[100] = {0};
              static char cborcryptosuite[2] = {0x81,0x00};

              memcpy(tempPayload, eapRespData, len);
              tempPayload[len]=cborcryptosuite[0];
              tempPayload[len+1]=cborcryptosuite[1];
              
              // 3rd parameter in set_reponse_payload is size_t length. Is the datatype size_t is unsigned integral type. It represents the size of any object in bytes and returned by sizeof operator. It is used for array indexing and counting. It can never be negative. The return type of strcspn, strlen functions is size_t.
              REST.set_response_payload(response, tempPayload, len+2);

              // see erbium.h in struct rest_implementation_status for the codes
              REST.set_response_status(response, REST.status.CREATED);
              REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
              REST.set_header_location(response, resource_hateoas.url);


              cipherSuitesSent = 1;

              printf("counterCryptoSuite = %d\n", counterCryptoSuite);
              counterCryptoSuite++;

              printf("eapResponse Data + Ciphersuites: ");
              printf_hex(tempPayload, len+2);
        }
    } 


  // executing this until the EAP key is available
  // need to see how to get that information!
  else {
        // Here we would verify the OSCORE Option

        unsigned char oscore_payload[10] = {0x19, 0xf7, 0xcc, 0x6a, 0x15, 0x20, 0x8b, 0x2d, 0xab};
        unsigned char testzero[1] = {0x00};
        
        // to do: add a option to the payload which needs to be sent to the controller.
        // addOption(response,COAP_OPTION_OSCORE, 0, testzero);
        // setPayload( response, oscore_payload, 9);

        printf("EAP Key is available!\n");
  }
}
#endif

/******************************************************************************/

// Defines for the EAP state machine
#define SEQ_LEN 22
#define KEY_LEN 16
#define AUTH_LEN 16

PROCESS(rest_server_example, "Erbium Example Server");
// PROCESS(coap_client_example, "COAP Client Example");
// AUTOSTART_PROCESSES(&rest_server_example, &coap_client_example);
AUTOSTART_PROCESSES(&rest_server_example);

PROCESS_THREAD(rest_server_example, ev, data)
{
  PROCESS_BEGIN();

  // Code for calling the EAP state machine
  unsigned char auth_key[KEY_LEN] = {0};
  unsigned char sequence[SEQ_LEN] = {0};

  memset(&msk_key,0, MSK_LENGTH);
	eapRestart=TRUE;
	eap_peer_sm_step(NULL);

  cipherSuitesSent = 0;

	memset(&auth_key, 0, AUTH_LEN);
	memset(&sequence, 0, SEQ_LEN);

	eapKeyAvailable = 0;

  // End of Code for the EAP state machine

  PRINTF("Starting Erbium Example Server\n");

  /* Initialize the REST engine. */
  rest_init_engine();

  /* Activate the application-specific resources. */

#if REST_RES_HATEOAS
  rest_activate_resource(&resource_hateoas);
#endif

/******************************************************************************/

    // waiting 15 seconds so that the tunslip tunnel is established and the rest engine loaded
    etimer_set(&et, 15 * CLOCK_SECOND);
    int inital_request_sent = 0;

    /* Define application-specific events here. */
    
    while (1)
    {
      PROCESS_WAIT_EVENT();

      // Sending the request only once
      if (etimer_expired(&et) && (inital_request_sent == 0)) {
        inital_request_sent = 1;
        static coap_packet_t initalRequest[1];
        
        SERVER_NODE(&server_ipaddr);

        coap_init_message(initalRequest, COAP_TYPE_NON, COAP_POST, 0);
        // sending the first and only request of the IoT device to the CoAP-EAP controllers resource /.well-known/a/
        coap_set_header_uri_path(initalRequest, "/.well-known/a");
        // setting the payload to the resources initial name
        coap_set_payload(initalRequest, &urlString, sizeof(urlString));
        // acutally sending the CoAP request
        COAP_BLOCKING_REQUEST(&server_ipaddr, REMOTE_PORT, initalRequest, NULL);
    }

  }    /* while (1) */

  PROCESS_END();
}