#
# Makefile
#
# Version:	$Id: Makefile,v 1.2 2007/11/25 13:56:10 aland Exp $
#

TARGET      = rlm_eap_sim
SRCS        = rlm_eap_sim.c
HEADERS     =
RLM_CFLAGS  = $(INCLTDL) -I../.. -I../../libeap
RLM_LIBS    =  ../../libeap/$(LIBPREFIX)freeradius-eap.la
RLM_INSTALL =

RLM_DIR=../../
include ${RLM_DIR}../rules.mak

$(LT_OBJS): $(HEADERS)
