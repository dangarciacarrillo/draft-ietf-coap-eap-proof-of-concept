#
# Makefile
#
# Version:	$Id: Makefile,v 1.5 2006/01/22 21:46:38 nbk Exp $
#

TARGET      = rlm_eap_leap
SRCS        = rlm_eap_leap.c eap_leap.c smbdes.c
RLM_CFLAGS  = $(INCLTDL) -I../.. -I../../libeap
HEADERS     = eap_leap.h  ../../eap.h ../../rlm_eap.h
RLM_INSTALL =

RLM_DIR=../../
include ${RLM_DIR}../rules.mak

$(LT_OBJS): $(HEADERS)
