/* modpriv.h: Stuff needed by both modules.c and modcall.c, but should not be
 * accessed from anywhere else.
 *
 * Version: $Id: modpriv.h,v 1.8 2007/11/23 09:06:05 aland Exp $ */
#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include "ltdl.h"

/*
 *	Keep track of which modules we've loaded.
 */
typedef struct module_entry_t {
	char			name[MAX_STRING_LEN];
	const module_t		*module;
	lt_dlhandle		handle;
} module_entry_t;

/*
 *	Per-instance data structure, to correlate the modules
 *	with the instance names (may NOT be the module names!),
 *	and the per-instance data structures.
 */
typedef struct module_instance_t {
	char			name[MAX_STRING_LEN];
	module_entry_t		*entry;
	void                    *insthandle;
#ifdef HAVE_PTHREAD_H
	pthread_mutex_t		*mutex;
#endif
	void			*old_insthandle[16];
} module_instance_t;

module_instance_t *find_module_instance(CONF_SECTION *, const char *instname);
