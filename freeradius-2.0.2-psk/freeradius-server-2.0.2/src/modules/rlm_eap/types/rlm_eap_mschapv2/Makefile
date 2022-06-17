#
# Makefile
#
# Version:	$Id: Makefile,v 1.1 2006/02/03 11:49:18 nbk Exp $
#

TARGET      = rlm_eap_mschapv2
SRCS        = rlm_eap_mschapv2.c
HEADERS     = eap_mschapv2.h
RLM_CFLAGS  = $(INCLTDL) -I../.. -I../../libeap
RLM_INSTALL =

RLM_DIR=../../
include ${RLM_DIR}../rules.mak

$(LT_OBJS): $(HEADERS)
