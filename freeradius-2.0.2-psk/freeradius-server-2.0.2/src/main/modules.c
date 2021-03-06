/*
 * modules.c	Radius module support.
 *
 * Version:	$Id: modules.c,v 1.156 2007/12/31 12:46:04 aland Exp $
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2003,2006  The FreeRADIUS server project
 * Copyright 2000  Alan DeKok <aland@ox.org>
 * Copyright 2000  Alan Curry <pacman@world.std.com>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: modules.c,v 1.156 2007/12/31 12:46:04 aland Exp $")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modpriv.h>
#include <freeradius-devel/modcall.h>
#include <freeradius-devel/rad_assert.h>

extern int check_config;

typedef struct indexed_modcallable {
	const		char *server;
	int		comp;
	int		idx;
	modcallable	*modulelist;
} indexed_modcallable;

/*
 *	For each component, keep an ordered list of ones to call.
 */
static rbtree_t *components = NULL;

static rbtree_t *module_tree = NULL;

static rbtree_t *instance_tree = NULL;

typedef struct section_type_value_t {
	const char	*section;
	const char	*typename;
	int		attr;
} section_type_value_t;


/*
 *	Ordered by component
 */
static const section_type_value_t section_type_value[RLM_COMPONENT_COUNT] = {
	{ "authenticate", "Auth-Type",       PW_AUTH_TYPE },
	{ "authorize",    "Autz-Type",       PW_AUTZ_TYPE },
	{ "preacct",      "Pre-Acct-Type",   PW_PRE_ACCT_TYPE },
	{ "accounting",   "Acct-Type",       PW_ACCT_TYPE },
	{ "session",      "Session-Type",    PW_SESSION_TYPE },
	{ "pre-proxy",    "Pre-Proxy-Type",  PW_PRE_PROXY_TYPE },
	{ "post-proxy",   "Post-Proxy-Type", PW_POST_PROXY_TYPE },
	{ "post-auth",    "Post-Auth-Type",  PW_POST_AUTH_TYPE },
};

static void indexed_modcallable_free(void *data)
{
	indexed_modcallable *c = data;

	modcallable_free(&c->modulelist);
	free(c);
}

static int indexed_modcallable_cmp(const void *one, const void *two)
{
	int rcode;
	const indexed_modcallable *a = one;
	const indexed_modcallable *b = two;

	if (a->server && !b->server) return -1;
	if (!a->server && b->server) return +1;
	if (a->server && b->server) {
		rcode = strcmp(a->server, b->server);
		if (rcode != 0) return rcode;
	}

	if (a->comp < b->comp) return -1;
	if (a->comp >  b->comp) return +1;

	return a->idx - b->idx;
}


/*
 *	Compare two module entries
 */
static int module_instance_cmp(const void *one, const void *two)
{
	const module_instance_t *a = one;
	const module_instance_t *b = two;

	return strcmp(a->name, b->name);
}


/*
 *	Free a module instance.
 */
static void module_instance_free(void *data)
{
	module_instance_t *this = data;

	if (this->entry->module->detach) {
		int i;

		(this->entry->module->detach)(this->insthandle);
		for (i = 0; i < 16; i++) {
			if (this->old_insthandle[i]) {
				(this->entry->module->detach)(this->old_insthandle[i]);
			}
		}
	}

#ifdef HAVE_PTHREAD_H
	if (this->mutex) {
		/*
		 *	FIXME
		 *	The mutex MIGHT be locked...
		 *	we'll check for that later, I guess.
		 */
		pthread_mutex_destroy(this->mutex);
		free(this->mutex);
	}
#endif
	memset(this, 0, sizeof(*this));
	free(this);
}


/*
 *	Compare two module entries
 */
static int module_entry_cmp(const void *one, const void *two)
{
	const module_entry_t *a = one;
	const module_entry_t *b = two;

	return strcmp(a->name, b->name);
}

/*
 *	Free a module entry.
 */
static void module_entry_free(void *data)
{
	module_entry_t *this = data;

	lt_dlclose(this->handle);	/* ignore any errors */
	memset(this, 0, sizeof(*this));
	free(this);
}


/*
 *	Remove the module lists.
 */
int detach_modules(void)
{
	rbtree_free(instance_tree);
	rbtree_free(components);
	rbtree_free(module_tree);

	lt_dlexit();

	return 0;
}


/*
 *	Find a module on disk or in memory, and link to it.
 */
static module_entry_t *linkto_module(const char *module_name,
				     CONF_SECTION *cs)
{
	module_entry_t myentry;
	module_entry_t *node;
	lt_dlhandle handle;
	char module_struct[256];
	char *p;
	const module_t *module;

	strlcpy(myentry.name, module_name, sizeof(myentry.name));
	node = rbtree_finddata(module_tree, &myentry);
	if (node) return node;

	/*
	 *	Keep the handle around so we can dlclose() it.
	 */
	handle = lt_dlopenext(module_name);
	if (handle == NULL) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Failed to link to module '%s': %s\n",
			   module_name, lt_dlerror());
		return NULL;
	}

	/*
	 *	Link to the module's rlm_FOO{} module structure.
	 *
	 *	The module_name variable has the version number
	 *	embedded in it, and we don't want that here.
	 */
	strcpy(module_struct, module_name);
	p = strrchr(module_struct, '-');
	if (p) *p = '\0';

	DEBUG3("    (Loaded %s, checking if it's valid)", module_name);

	/*
	 *	libltld MAY core here, if the handle it gives us contains
	 *	garbage data.
	 */
	module = lt_dlsym(handle, module_struct);
	if (!module) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Failed linking to %s structure: %s\n",
			   module_name, lt_dlerror());
		lt_dlclose(handle);
		return NULL;
	}
	/*
	 *	Before doing anything else, check if it's sane.
	 */
	if (module->magic != RLM_MODULE_MAGIC_NUMBER) {
		lt_dlclose(handle);
		cf_log_err(cf_sectiontoitem(cs),
			   "Invalid version in module '%s'",
			   module_name);
		return NULL;

	}

	/* make room for the module type */
	node = rad_malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));
	strlcpy(node->name, module_name, sizeof(node->name));
	node->module = module;
	node->handle = handle;

	cf_log_module(cs, "Linked to module %s", module_name);

	/*
	 *	Add the module as "rlm_foo-version" to the configuration
	 *	section.
	 */
	if (!rbtree_insert(module_tree, node)) {
		radlog(L_ERR, "Failed to cache module %s", module_name);
		lt_dlclose(handle);
		free(node);
		return NULL;
	}

	return node;
}

/*
 *	Find a module instance.
 */
module_instance_t *find_module_instance(CONF_SECTION *modules,
					const char *instname)
{
	CONF_SECTION *cs;
	const char *name1, *name2;
	module_instance_t *node, myNode;
	char module_name[256];

	if (!modules) return NULL;

	/*
	 *	Module instances are declared in the modules{} block
	 *	and referenced later by their name, which is the
	 *	name2 from the config section, or name1 if there was
	 *	no name2.
	 */
	cs = cf_section_sub_find_name2(modules, NULL, instname);
	if (cs == NULL) {
		radlog(L_ERR, "ERROR: Cannot find a configuration entry for module \"%s\".\n", instname);
		return NULL;
	}

	/*
	 *	If there's already a module instance, return it.
	 */
	strlcpy(myNode.name, instname, sizeof(myNode.name));
	node = rbtree_finddata(instance_tree, &myNode);
	if (node) return node;

	name1 = cf_section_name1(cs);
	name2 = cf_section_name2(cs);

	/*
	 *	Found the configuration entry.
	 */
	node = rad_malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));

	node->insthandle = NULL;

	/*
	 *	Names in the "modules" section aren't prefixed
	 *	with "rlm_", so we add it here.
	 */
	snprintf(module_name, sizeof(module_name), "rlm_%s", name1);

	node->entry = linkto_module(module_name, cs);
	if (!node->entry) {
		free(node);
		/* linkto_module logs any errors */
		return NULL;
	}

	if (check_config && (node->entry->module->instantiate) &&
	    (node->entry->module->type & RLM_TYPE_CHECK_CONFIG_SAFE) == 0) {
		cf_log_module(cs, "Skipping instantiation of %s", instname);
	} else {
		cf_log_module(cs, "Instantiating %s", instname);
	}

	/*
	 *	Call the module's instantiation routine.
	 */
	if ((node->entry->module->instantiate) &&
	    (!check_config ||
	     ((node->entry->module->type & RLM_TYPE_CHECK_CONFIG_SAFE) != 0)) &&
	    ((node->entry->module->instantiate)(cs, &node->insthandle) < 0)) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Instantiation failed for module \"%s\"",
			   instname);
		free(node);
		return NULL;
	}

	/*
	 *	We're done.  Fill in the rest of the data structure,
	 *	and link it to the module instance list.
	 */
	strlcpy(node->name, instname, sizeof(node->name));

#ifdef HAVE_PTHREAD_H
	/*
	 *	If we're threaded, check if the module is thread-safe.
	 *
	 *	If it isn't, we create a mutex.
	 */
	if ((node->entry->module->type & RLM_TYPE_THREAD_UNSAFE) != 0) {
		node->mutex = (pthread_mutex_t *) rad_malloc(sizeof(pthread_mutex_t));
		/*
		 *	Initialize the mutex.
		 */
		pthread_mutex_init(node->mutex, NULL);
	} else {
		/*
		 *	The module is thread-safe.  Don't give it a mutex.
		 */
		node->mutex = NULL;
	}

#endif
	rbtree_insert(instance_tree, node);

	return node;
}

static indexed_modcallable *lookup_by_index(const char *server, int comp,
					    int idx)
{
	indexed_modcallable myc;
	
	myc.comp = comp;
	myc.idx = idx;
	myc.server = server;

	return rbtree_finddata(components, &myc);
}

/*
 *	Create a new sublist.
 */
static indexed_modcallable *new_sublist(const char *server, int comp, int idx)
{
	indexed_modcallable *c;

	c = lookup_by_index(server, comp, idx);

	/* It is an error to try to create a sublist that already
	 * exists. It would almost certainly be caused by accidental
	 * duplication in the config file.
	 *
	 * index 0 is the exception, because it is used when we want
	 * to collect _all_ listed modules under a single index by
	 * default, which is currently the case in all components
	 * except authenticate. */
	if (c) {
		if (idx == 0) {
			return c;
		}
		return NULL;
	}

	c = rad_malloc(sizeof(*c));
	c->modulelist = NULL;
	c->server = server;
	c->comp = comp;
	c->idx = idx;

	if (!rbtree_insert(components, c)) {
		free(c);
		return NULL;
	}

	return c;
}

static int indexed_modcall(int comp, int idx, REQUEST *request)
{
	int rcode;
	indexed_modcallable *this;
	modcallable *list = NULL;

	this = lookup_by_index(request->server, comp, idx);
	if (!this) {
		if (idx != 0) DEBUG2("  WARNING: Unknown value specified for %s.  Cannot perform requested action.",
				     section_type_value[comp].typename);
	} else {
		list = this->modulelist;
	}

	request->component = section_type_value[comp].section;

	rcode = modcall(comp, list, request);

	request->module = "<server-core>";
	request->component = "<server-core>";
	return rcode;
}

/*
 *	Load a sub-module list, as found inside an Auth-Type foo {}
 *	block
 */
static int load_subcomponent_section(modcallable *parent, CONF_SECTION *cs,
				     const char *server, int comp)
{
	indexed_modcallable *subcomp;
	modcallable *ml;
	DICT_VALUE *dval;
	const char *name2 = cf_section_name2(cs);

	rad_assert(comp >= RLM_COMPONENT_AUTH);
	rad_assert(comp <= RLM_COMPONENT_COUNT);

	/*
	 *	Sanity check.
	 */
	if (!name2) {
		cf_log_err(cf_sectiontoitem(cs),
			   "No name specified for %s block",
			   section_type_value[comp].typename);
		return 1;
	}

	/*
	 *	Compile the group.
	 */
	ml = compile_modgroup(parent, comp, cs);
	if (!ml) {
		return 0;
	}

	/*
	 *	We must assign a numeric index to this subcomponent.
	 *	It is generated and placed in the dictionary
	 *	automatically.  If it isn't found, it's a serious
	 *	error.
	 */
	dval = dict_valbyname(section_type_value[comp].attr, name2);
	if (!dval) {
		cf_log_err(cf_sectiontoitem(cs),
			   "%s %s Not previously configured",
			   section_type_value[comp].typename, name2);
		modcallable_free(&ml);
		return 0;
	}

	subcomp = new_sublist(server, comp, dval->value);
	if (!subcomp) {
		modcallable_free(&ml);
		return 1;
	}

	subcomp->modulelist = ml;
	return 1;		/* OK */
}

static int define_type(const DICT_ATTR *dattr, const char *name)
{
	uint32_t value;
	DICT_VALUE *dval;

	/*
	 *	If the value already exists, don't
	 *	create it again.
	 */
	dval = dict_valbyname(dattr->attr, name);
	if (dval) return 1;

	/*
	 *	Create a new unique value with a
	 *	meaningless number.  You can't look at
	 *	it from outside of this code, so it
	 *	doesn't matter.  The only requirement
	 *	is that it's unique.
	 */
	do {
		value = fr_rand() & 0x00ffffff;
	} while (dict_valbyattr(dattr->attr, value));

	if (dict_addvalue(name, dattr->name, value) < 0) {
		radlog(L_ERR, "%s", librad_errstr);
		return 0;
	}

	return 1;
}

static int load_component_section(CONF_SECTION *cs,
				  const char *server, int comp)
{
	modcallable *this;
	CONF_ITEM *modref;
	int idx;
	indexed_modcallable *subcomp;
	const char *modname;
	const char *visiblename;
	const DICT_ATTR *dattr;

	/*
	 *	Find the attribute used to store VALUEs for this section.
	 */
	dattr = dict_attrbyvalue(section_type_value[comp].attr);
	if (!dattr) {
		cf_log_err(cf_sectiontoitem(cs),
			   "No such attribute %s",
			   section_type_value[comp].typename);
		return -1;
	}

	/*
	 *	Loop over the entries in the named section, loading
	 *	the sections this time.
	 */
	for (modref = cf_item_find_next(cs, NULL);
	     modref != NULL;
	     modref = cf_item_find_next(cs, modref)) {
		const char *name1;
		CONF_PAIR *cp = NULL;
		CONF_SECTION *scs = NULL;

		if (cf_item_is_section(modref)) {
			scs = cf_itemtosection(modref);

			name1 = cf_section_name1(scs);

			if (strcmp(name1,
				   section_type_value[comp].typename) == 0) {
				if (!load_subcomponent_section(NULL, scs,
							       server, comp)) {
					return -1; /* FIXME: memleak? */
				}
				continue;
			}

			cp = NULL;

		} else if (cf_item_is_pair(modref)) {
			cp = cf_itemtopair(modref);

		} else {
			continue; /* ignore it */
		}

		/*
		 *	Try to compile one entry.
		 */
		this = compile_modsingle(NULL, comp, modref, &modname);
		if (!this) {
			cf_log_err(cf_sectiontoitem(cs),
				   "Errors parsing %s section.\n",
				   cf_section_name1(cs));
			return -1;
		}

		/*
		 *	Look for Auth-Type foo {}, which are special
		 *	cases of named sections, and allowable ONLY
		 *	at the top-level.
		 *
		 *	i.e. They're not allowed in a "group" or "redundant"
		 *	subsection.
		 */
		if (comp == RLM_COMPONENT_AUTH) {
			DICT_VALUE *dval;
			const char *modrefname = NULL;
			if (cp) {
				modrefname = cf_pair_attr(cp);
			} else {
				modrefname = cf_section_name2(scs);
				if (!modrefname) {
					modcallable_free(&this);
					cf_log_err(cf_sectiontoitem(cs),
						   "Errors parsing %s sub-section.\n",
						   cf_section_name1(scs));
					return -1;
				}
			}

			dval = dict_valbyname(PW_AUTH_TYPE, modrefname);
			if (!dval) {
				/*
				 *	It's a section, but nothing we
				 *	recognize.  Die!
				 */
				modcallable_free(&this);
				cf_log_err(cf_sectiontoitem(cs),
					   "Unknown Auth-Type \"%s\" in %s sub-section.",
					   modrefname, section_type_value[comp].section);
				return -1;
			}
			idx = dval->value;
		} else {
			/* See the comment in new_sublist() for explanation
			 * of the special index 0 */
			idx = 0;
		}

		subcomp = new_sublist(server, comp, idx);
		if (subcomp == NULL) {
			modcallable_free(&this);
			continue;
		}

		/* If subcomp->modulelist is NULL, add_to_modcallable will
		 * create it */
		visiblename = cf_section_name2(cs);
		if (visiblename == NULL)
			visiblename = cf_section_name1(cs);
		add_to_modcallable(&subcomp->modulelist, this,
				   comp, visiblename);
	}

	return 0;
}

static int load_byserver(CONF_SECTION *cs)
{
	int comp, flag;
	const char *server = cf_section_name2(cs);

	cf_log_info(cs, " modules {");

	/*
	 *	Define types first.
	 */
	for (comp = 0; comp < RLM_COMPONENT_COUNT; ++comp) {
		CONF_SECTION *subcs;
		CONF_ITEM *modref;
		DICT_ATTR *dattr;

		subcs = cf_section_sub_find(cs,
					    section_type_value[comp].section);
		if (!subcs) continue;
			
		if (cf_item_find_next(subcs, NULL) == NULL) continue;

		/*
		 *	Find the attribute used to store VALUEs for this section.
		 */
		dattr = dict_attrbyvalue(section_type_value[comp].attr);
		if (!dattr) {
			cf_log_err(cf_sectiontoitem(subcs),
				   "No such attribute %s",
				   section_type_value[comp].typename);
			cf_log_info(cs, " }");
			return -1;
		}

		/*
		 *	Define dynamic types, so that others can reference
		 *	them.
		 */
		for (modref = cf_item_find_next(subcs, NULL);
		     modref != NULL;
		     modref = cf_item_find_next(subcs, modref)) {
			const char *name1;
			CONF_SECTION *subsubcs;

			/*
			 *	Create types for simple references
			 *	only when parsing the authenticate
			 *	section.
			 */
			if ((section_type_value[comp].attr == PW_AUTH_TYPE) &&
			    cf_item_is_pair(modref)) {
				CONF_PAIR *cp = cf_itemtopair(modref);
				if (!define_type(dattr, cf_pair_attr(cp))) {
					return -1;
				}

				continue;
			}

			if (!cf_item_is_section(modref)) continue;
			
			subsubcs = cf_itemtosection(modref);
			name1 = cf_section_name1(subsubcs);
		
			if (strcmp(name1, section_type_value[comp].typename) == 0) {
				if (!define_type(dattr,
						 cf_section_name2(subsubcs))) {
					cf_log_info(cs, " }");
					return -1;
				}
			}
		}
	} /* loop over components */

	/*
	 *	Loop over all of the known components, finding their
	 *	configuration section, and loading it.
	 */
	flag = 0;
	for (comp = 0; comp < RLM_COMPONENT_COUNT; ++comp) {
		CONF_SECTION *subcs;

		subcs = cf_section_sub_find(cs,
					    section_type_value[comp].section);
		if (!subcs) continue;
			
		if (cf_item_find_next(subcs, NULL) == NULL) continue;
			
		cf_log_module(cs, "Checking %s {...} for more modules to load",
		       section_type_value[comp].section);

		if (load_component_section(subcs, server, comp) < 0) {
			cf_log_info(cs, " }");
			return -1;
		}
		flag = 1;
	} /* loop over components */

	/*
	 *	We haven't loaded any of the normal sections.  Maybe we're
	 *	supposed to load the vmps section.
	 *
	 *	This is a bit of a hack...
	 */
	if (!flag) {
		CONF_SECTION *subcs;

		subcs = cf_section_sub_find(cs, "vmps");
		if (subcs) {
			cf_log_module(cs, "Checking vmps {...} for more modules to load");		
			if (load_component_section(subcs, server,
						   RLM_COMPONENT_POST_AUTH) < 0) {
				return -1;
			}
			flag = 1;
		}
	}

	cf_log_info(cs, " }");

	if (!flag && server) {
		DEBUG("WARNING: Server %s is empty, and will do nothing!",
		      server);
	}

	return 0;
}


int module_hup(CONF_SECTION *modules)
{
	time_t when;
	CONF_ITEM *ci;
	CONF_SECTION *cs;
	module_instance_t *node;

	static int insthandle_counter = -1;
	static time_t hup_times[16];

	if (!modules) return 0;

	if (insthandle_counter < 0) {
		memset(hup_times, 0, sizeof(hup_times));
		insthandle_counter = 0;
	}

	when = time(NULL);

	/*
	 *	Loop over the modules
	 */
	for (ci=cf_item_find_next(modules, NULL);
	     ci != NULL;
	     ci=cf_item_find_next(modules, ci)) {
		int i;
		void *insthandle = NULL;
		const char *instname;
		module_instance_t myNode;

		/*
		 *	If it's not a section, ignore it.
		 */
		if (!cf_item_is_section(ci)) continue;

		cs = cf_itemtosection(ci);
		instname = cf_section_name2(cs);
		if (!instname) instname = cf_section_name1(cs);

		strlcpy(myNode.name, instname, sizeof(myNode.name));
		node = rbtree_finddata(instance_tree, &myNode);
		if (!node ||
		    !node->entry->module->instantiate ||
		    ((node->entry->module->type & RLM_TYPE_HUP_SAFE) == 0)) {
			continue;
		}

		cf_log_module(cs, "Trying to reload module \"%s\"", node->name);

		if ((node->entry->module->instantiate)(cs, &insthandle) < 0) {
			cf_log_err(cf_sectiontoitem(cs),
				   "HUP failed for module \"%s\".  Using old configuration.",
				   node->name);
			continue;
		}

		radlog(L_INFO, " Module: Reloaded module \"%s\"", node->name);

		/*
		 *	Free all configurations old enough to be
		 *	deleted.  This is slightly wasteful, but easy
		 *	to do.
		 *
		 *	We permit HUPs every 5s, and we cache the last
		 *	16 configurations.  So the current entry at
		 *	insthandle_counter will either be empty, OR will
		 *	be at least (5*16) = 80s old.  The following check
		 *	ensures that it will be deleted.
		 */
		for (i = 0; i < 16; i++) {
			if ((int) (when - hup_times[insthandle_counter]) < 60)
				continue;

			if (!node->old_insthandle[i]) continue;

			cf_section_parse_free(cs, node->old_insthandle[i]);
			
			if (node->entry->module->detach) {
				(node->entry->module->detach)(node->old_insthandle[i]);
			}
			node->old_insthandle[i] = NULL;
		}

		/*
		 *	Save the old instance handle for later deletion.
		 */
		node->old_insthandle[insthandle_counter] = node->insthandle;
		node->insthandle = insthandle;

		/*
		 *	FIXME: Set a timeout to come back in 60s, so that
		 *	we can pro-actively clean up the old instances.
		 */
	}

	/*
	 *	Remember when we were last HUP'd.
	 */
	hup_times[insthandle_counter] = when;
	insthandle_counter++;
	insthandle_counter &= 0x0f;

	return 1;
}


/*
 *	Parse the module config sections, and load
 *	and call each module's init() function.
 *
 *	Libtool makes your life a LOT easier, especially with libltdl.
 *	see: http://www.gnu.org/software/libtool/
 */
int setup_modules(int reload, CONF_SECTION *config)
{
	CONF_SECTION	*cs, *modules;
	rad_listen_t	*listener;
	int		null_server = FALSE;

	if (reload) return 0;

	/*
	 *	If necessary, initialize libltdl.
	 */
	if (!reload) {
		/*
		 *	Set the default list of preloaded symbols.
		 *	This is used to initialize libltdl's list of
		 *	preloaded modules.
		 *
		 *	i.e. Static modules.
		 */
		LTDL_SET_PRELOADED_SYMBOLS();

		if (lt_dlinit() != 0) {
			radlog(L_ERR, "Failed to initialize libraries: %s\n",
					lt_dlerror());
			return -1;
		}

		/*
		 *	Set the search path to ONLY our library directory.
		 *	This prevents the modules from being found from
		 *	any location on the disk.
		 */
		lt_dlsetsearchpath(radlib_dir);

		/*
		 *	Set up the internal module struct.
		 */
		module_tree = rbtree_create(module_entry_cmp,
					    module_entry_free, 0);
		if (!module_tree) {
			radlog(L_ERR, "Failed to initialize modules\n");
			return -1;
		}

		instance_tree = rbtree_create(module_instance_cmp,
					      module_instance_free, 0);
		if (!instance_tree) {
			radlog(L_ERR, "Failed to initialize modules\n");
			return -1;
		}
	}

	components = rbtree_create(indexed_modcallable_cmp,
				   indexed_modcallable_free, 0);
	if (!components) {
		radlog(L_ERR, "Failed to initialize components\n");
		return -1;
	}

	/*
	 *	Remember where the modules were stored.
	 */
	modules = cf_section_sub_find(config, "modules");
	if (!modules) {
		radlog(L_ERR, "Cannot find a \"modules\" section in the configuration file!");
		return -1;
	}

	DEBUG2("%s: #### Instantiating modules ####", mainconfig.name);

	/*
	 *  Look for the 'instantiate' section, which tells us
	 *  the instantiation order of the modules, and also allows
	 *  us to load modules with no authorize/authenticate/etc.
	 *  sections.
	 */
	cs = cf_section_sub_find(config, "instantiate");
	if (cs != NULL) {
		CONF_ITEM *ci;
		CONF_PAIR *cp;
		module_instance_t *module;
		const char *name;

		cf_log_info(cs, " instantiate {");

		/*
		 *  Loop over the items in the 'instantiate' section.
		 */
		for (ci=cf_item_find_next(cs, NULL);
		     ci != NULL;
		     ci=cf_item_find_next(cs, ci)) {

			/*
			 *	Skip sections.  They'll be handled
			 *	later, if they're referenced at all...
			 */
			if (cf_item_is_section(ci)) {
				continue;
			}

			cp = cf_itemtopair(ci);
			name = cf_pair_attr(cp);
			module = find_module_instance(modules, name);
			if (!module) {
				return -1;
			}
		} /* loop over items in the subsection */

		cf_log_info(cs, " }");
	} /* if there's an 'instantiate' section. */

	/*
	 *	Loop over the listeners, figuring out which sections
	 *	to load.
	 */
	for (listener = mainconfig.listen;
	     listener != NULL;
	     listener = listener->next) {
		char buffer[256];

		if (listener->type == RAD_LISTEN_PROXY) continue;

		cs = cf_section_sub_find_name2(config,
					       "server", listener->server);
		if (!cs && (listener->server != NULL)) {
			listener->print(listener, buffer, sizeof(buffer));

			radlog(L_ERR, "No server has been defined for %s", buffer);
			return -1;
		}
	}

	DEBUG2("%s: #### Loading Virtual Servers ####", mainconfig.name);

	/*
	 *	Load all of the virtual servers.
	 */
	for (cs = cf_subsection_find_next(config, NULL, "server");
	     cs != NULL;
	     cs = cf_subsection_find_next(config, cs, "server")) {
		const char *name2 = cf_section_name2(cs);

		if (name2) {
			cf_log_info(cs, "server %s {", name2);
		} else {
			cf_log_info(cs, "server {");
			null_server = TRUE;
		}
		if (load_byserver(cs) < 0) {
			cf_log_info(cs, "}");
			return -1;
		}
		cf_log_info(cs, "}");
	}

	/*
	 *	No empty server defined.  Try to load an old-style
	 *	one for backwards compatibility.
	 */
	if (!null_server) {
		cf_log_info(cs, "server {");
		if (load_byserver(config) < 0) {
			cf_log_info(cs, "}");
			return -1;
		}
		cf_log_info(cs, "}");
	}

	return 0;
}

/*
 *	Call all authorization modules until one returns
 *	somethings else than RLM_MODULE_OK
 */
int module_authorize(int autz_type, REQUEST *request)
{
	return indexed_modcall(RLM_COMPONENT_AUTZ, autz_type, request);
}

/*
 *	Authenticate a user/password with various methods.
 */
int module_authenticate(int auth_type, REQUEST *request)
{
	return indexed_modcall(RLM_COMPONENT_AUTH, auth_type, request);
}

/*
 *	Do pre-accounting for ALL configured sessions
 */
int module_preacct(REQUEST *request)
{
	return indexed_modcall(RLM_COMPONENT_PREACCT, 0, request);
}

/*
 *	Do accounting for ALL configured sessions
 */
int module_accounting(int acct_type, REQUEST *request)
{
	return indexed_modcall(RLM_COMPONENT_ACCT, acct_type, request);
}

/*
 *	See if a user is already logged in.
 *
 *	Returns: 0 == OK, 1 == double logins, 2 == multilink attempt
 */
int module_checksimul(int sess_type, REQUEST *request, int maxsimul)
{
	int rcode;

	if(!request->username)
		return 0;

	request->simul_count = 0;
	request->simul_max = maxsimul;
	request->simul_mpp = 1;

	rcode = indexed_modcall(RLM_COMPONENT_SESS, sess_type, request);

	if (rcode != RLM_MODULE_OK) {
		/* FIXME: Good spot for a *rate-limited* warning to the log */
		return 0;
	}

	return (request->simul_count < maxsimul) ? 0 : request->simul_mpp;
}

/*
 *	Do pre-proxying for ALL configured sessions
 */
int module_pre_proxy(int type, REQUEST *request)
{
	return indexed_modcall(RLM_COMPONENT_PRE_PROXY, type, request);
}

/*
 *	Do post-proxying for ALL configured sessions
 */
int module_post_proxy(int type, REQUEST *request)
{
	return indexed_modcall(RLM_COMPONENT_POST_PROXY, type, request);
}

/*
 *	Do post-authentication for ALL configured sessions
 */
int module_post_auth(int postauth_type, REQUEST *request)
{
	return indexed_modcall(RLM_COMPONENT_POST_AUTH, postauth_type, request);
}
