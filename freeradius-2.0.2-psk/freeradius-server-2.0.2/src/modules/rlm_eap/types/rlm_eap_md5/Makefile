#
# Makefile
#
# Version:	$Id: Makefile,v 1.4 2006/02/03 11:49:18 nbk Exp $
#

TARGET      = rlm_eap_md5
SRCS        = rlm_eap_md5.c eap_md5.c
HEADERS     = eap_md5.h
RLM_CFLAGS  = $(INCLTDL) -I../.. -I../../libeap
RLM_INSTALL =

RLM_DIR=../../
include ${RLM_DIR}../rules.mak

$(LT_OBJS): $(HEADERS)
