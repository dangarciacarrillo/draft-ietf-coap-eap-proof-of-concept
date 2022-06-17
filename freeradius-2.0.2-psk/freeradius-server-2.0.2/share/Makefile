#
#	Scripts to format dictionary files.
#
#	$Id: Makefile,v 1.1 2005/08/08 20:22:14 aland Exp $
#
.PHONY: format

#
#  This should only be run by hand, and then sanity checked by hand!
#
format: dictionary*
	@for x in dictionary* ; do \
		cat $$x | ./format.pl > tmp; \
		mv tmp $$x; \
	done
