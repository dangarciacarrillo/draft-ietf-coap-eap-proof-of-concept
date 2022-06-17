/**
 * @file coap_eap_statemachine.h
 * @brief State machine's common functions headers.
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
 *  
 *  https://sourceforge.net/projects/openpana/
 */
#ifndef COAP_STATEMACHINE_H
#define COAP_STATEMACHINE_H

#include "include.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "session.h"
#include "statemachines/coap_eap_session.h"

#ifdef __cplusplus
}
#endif

 /** Macro to reference last message's type. If the message is NULL, type is set to 0.*/
#define LMTYPE ((current_session->LAST_MESSAGE!=NULL)?ntohs(((pana*)(current_session->LAST_MESSAGE))->msg_type):0)
/** Macro to reference last message's flags. The message cannot be NULL, it must be checked before.*/
#define LMFLAGS (ntohs(((pana*)(current_session->LAST_MESSAGE))->flags))

/** General callback function definition, it corresponds to a function
 * to be called in a position of the state machine table */
//typedef int (*sm_action)();

/** State transition table is used to represent the operation of the 
 * protocol by a number of cooperating state machines each comprising a 
 * group of connected, mutually exclusive states. Only one state of each
 * machine can be active at any given time. Rows are the states and
 * columns are the events. By invoking the table with a state and
 * associated event, the corresponding callback function is called. */
//sm_action table [NUM_STATES][NUM_EVENTS];

/** Pointer to the current CoAP EAP session.*/
coap_eap_ctx * current_session;


#endif
