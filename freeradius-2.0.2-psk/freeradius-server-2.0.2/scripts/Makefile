# $Id: Makefile,v 1.6 2005/07/21 08:07:48 nbk Exp $

include ../Make.inc

all:

radwatch:
	$(INSTALL) -m 755 radwatch		$(R)$(sbindir)

install:
	$(INSTALL) -m 755 rc.radiusd		$(R)$(sbindir)
	$(INSTALL) -m 755 radsqlrelay		$(R)$(bindir)
	$(INSTALL) -m 755 radwatch		$(R)$(sbindir)
	$(INSTALL) -m 755 check-radiusd-config	$(R)$(sbindir)

clean:
