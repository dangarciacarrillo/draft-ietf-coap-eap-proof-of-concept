/*
 * realms.c	Realm handling code
 *
 * Version:     $Id: realms.c,v 1.44 2008/02/10 15:23:33 aland Exp $
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
 * Copyright 2007  The FreeRADIUS server project
 * Copyright 2007  Alan DeKok <aland@deployingradius.com>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: realms.c,v 1.44 2008/02/10 15:23:33 aland Exp $")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/rad_assert.h>

#include <sys/stat.h>

#include <ctype.h>
#include <fcntl.h>

static rbtree_t *realms_byname = NULL;

static rbtree_t	*home_servers_byaddr = NULL;
static rbtree_t	*home_servers_byname = NULL;

static rbtree_t	*home_pools_byname = NULL;

typedef struct realm_config_t {
	CONF_SECTION	*cs;
	int		dead_time;
	int		retry_count;
	int		retry_delay;
	int		fallback;
	int		wake_all_if_all_dead;
} realm_config_t;

static realm_config_t *realm_config = NULL;

/*
 *  Map the proxy server configuration parameters to variables.
 */
static const CONF_PARSER proxy_config[] = {
	{ "retry_delay",  PW_TYPE_INTEGER,
	  offsetof(realm_config_t, retry_delay),
	  NULL, Stringify(RETRY_DELAY) },

	{ "retry_count",  PW_TYPE_INTEGER,
	  offsetof(realm_config_t, retry_count),
	  NULL, Stringify(RETRY_COUNT) },

	{ "default_fallback", PW_TYPE_BOOLEAN,
	  offsetof(realm_config_t, fallback),
	  NULL, "no" },

	{ "dead_time",    PW_TYPE_INTEGER, 
	  offsetof(realm_config_t, dead_time),
	  NULL, Stringify(DEAD_TIME) },

	{ "wake_all_if_all_dead", PW_TYPE_BOOLEAN,
	  offsetof(realm_config_t, wake_all_if_all_dead),
	  NULL, "no" },

	{ NULL, -1, 0, NULL, NULL }
};

static int realm_name_cmp(const void *one, const void *two)
{
	const REALM *a = one;
	const REALM *b = two;

	return strcasecmp(a->name, b->name);
}


static int home_server_name_cmp(const void *one, const void *two)
{
	const home_server *a = one;
	const home_server *b = two;

	if (a->type < b->type) return -1;
	if (a->type > b->type) return +1;

	return strcasecmp(a->name, b->name);
}

static int home_server_addr_cmp(const void *one, const void *two)
{
	const home_server *a = one;
	const home_server *b = two;

	if (a->port < b->port) return -1;
	if (a->port > b->port) return +1;

	return fr_ipaddr_cmp(&a->ipaddr, &b->ipaddr);
}


static int home_pool_name_cmp(const void *one, const void *two)
{
	const home_pool_t *a = one;
	const home_pool_t *b = two;

	if (a->server_type < b->server_type) return -1;
	if (a->server_type > b->server_type) return +1;

	return strcasecmp(a->name, b->name);
}


/*
 *	Xlat for %{home_server:foo}
 */
static size_t xlat_home_server(UNUSED void *instance, REQUEST *request,
			       char *fmt, char *out, size_t outlen,
			       UNUSED RADIUS_ESCAPE_STRING func)
{
	const char *value = NULL;
	CONF_PAIR *cp;

	if (!fmt || !out || (outlen < 1)) return 0;

	if (!request || !request->home_server) {
		*out = '\0';
		return 0;
	}

	cp = cf_pair_find(request->home_server->cs, fmt);
	if (!cp || !(value = cf_pair_value(cp))) {
		*out = '\0';
		return 0;
	}
	
	strlcpy(out, value, outlen);

	return strlen(out);
}


/*
 *	Xlat for %{home_server_pool:foo}
 */
static size_t xlat_server_pool(UNUSED void *instance, REQUEST *request,
			       char *fmt, char *out, size_t outlen,
			       UNUSED RADIUS_ESCAPE_STRING func)
{
	const char *value = NULL;
	CONF_PAIR *cp;

	if (!fmt || !out || (outlen < 1)) return 0;

	if (!request || !request->home_pool) {
		*out = '\0';
		return 0;
	}

	cp = cf_pair_find(request->home_pool->cs, fmt);
	if (!cp || !(value = cf_pair_value(cp))) {
		*out = '\0';
		return 0;
	}
	
	strlcpy(out, value, outlen);

	return strlen(out);
}


void realms_free(void)
{
	rbtree_free(home_servers_byname);
	home_servers_byname = NULL;

	rbtree_free(home_servers_byaddr);
	home_servers_byaddr = NULL;

	rbtree_free(home_pools_byname);
	home_pools_byname = NULL;

	rbtree_free(realms_byname);
	realms_byname = NULL;

	free(realm_config);
}


static struct in_addr hs_ip4addr;
static struct in6_addr hs_ip6addr;
static char *hs_type = NULL;
static char *hs_check = NULL;

static CONF_PARSER home_server_config[] = {
	{ "ipaddr",  PW_TYPE_IPADDR,
	  0, &hs_ip4addr,  NULL },
	{ "ipv6addr",  PW_TYPE_IPV6ADDR,
	  0, &hs_ip6addr, NULL },

	{ "port", PW_TYPE_INTEGER,
	  offsetof(home_server,port), NULL,   "0" },

	{ "type",  PW_TYPE_STRING_PTR,
	  0, &hs_type, NULL },

	{ "secret",  PW_TYPE_STRING_PTR,
	  offsetof(home_server,secret), NULL,  NULL},

	{ "response_window", PW_TYPE_INTEGER,
	  offsetof(home_server,response_window), NULL,   "30" },
	{ "max_outstanding", PW_TYPE_INTEGER,
	  offsetof(home_server,max_outstanding), NULL,   "65536" },

	{ "zombie_period", PW_TYPE_INTEGER,
	  offsetof(home_server,zombie_period), NULL,   "40" },
	{ "status_check", PW_TYPE_STRING_PTR,
	  0, &hs_check,   "none" },
	{ "ping_check", PW_TYPE_STRING_PTR,
	  0, &hs_check,   "none" },

	{ "ping_interval", PW_TYPE_INTEGER,
	  offsetof(home_server,ping_interval), NULL,   "30" },
	{ "check_interval", PW_TYPE_INTEGER,
	  offsetof(home_server,ping_interval), NULL,   "30" },
	{ "num_answers_to_alive", PW_TYPE_INTEGER,
	  offsetof(home_server,num_pings_to_alive), NULL,   "3" },
	{ "num_pings_to_alive", PW_TYPE_INTEGER,
	  offsetof(home_server,num_pings_to_alive), NULL,   "3" },
	{ "revive_interval", PW_TYPE_INTEGER,
	  offsetof(home_server,revive_interval), NULL,   "300" },
	{ "status_check_timeout", PW_TYPE_INTEGER,
	  offsetof(home_server,ping_timeout), NULL,   "4" },

	{ "username",  PW_TYPE_STRING_PTR,
	  offsetof(home_server,ping_user_name), NULL,  NULL},
	{ "password",  PW_TYPE_STRING_PTR,
	  offsetof(home_server,ping_user_password), NULL,  NULL},

	{ NULL, -1, 0, NULL, NULL }		/* end the list */

};


static int home_server_add(CONF_SECTION *cs, int type)
{
	const char *name2;
	home_server *home;
	int dual = FALSE;

	name2 = cf_section_name1(cs);
	if (!name2 || (strcasecmp(name2, "home_server") != 0)) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Section is not a home_server.");
		return 0;
	}

	name2 = cf_section_name2(cs);
	if (!name2) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Home server section is missing a name.");
		return 0;
	}

	home = rad_malloc(sizeof(*home));
	memset(home, 0, sizeof(*home));

	home->name = name2;
	home->cs = cs;

	memset(&hs_ip4addr, 0, sizeof(hs_ip4addr));
	memset(&hs_ip6addr, 0, sizeof(hs_ip6addr));
	cf_section_parse(cs, home, home_server_config);

	if (!cf_pair_find(cs, "ipaddr") &&
	    !cf_pair_find(cs, "ipv6addr")) {
		cf_log_err(cf_sectiontoitem(cs),
			   "No IPv4 or IPv6 address defined for home server %s.",
			   name2);
		free(home);
		free(hs_type);
		hs_type = NULL;
		free(hs_check);
		hs_check = NULL;
		return 0;
	}

	/*
	 *	Figure out which one to use.
	 */
	if (cf_pair_find(cs, "ipaddr")) {
		home->ipaddr.af = AF_INET;
		home->ipaddr.ipaddr.ip4addr = hs_ip4addr;

	} else if (cf_pair_find(cs, "ipv6addr")) {
		home->ipaddr.af = AF_INET6;
		home->ipaddr.ipaddr.ip6addr = hs_ip6addr;

	} else {
		cf_log_err(cf_sectiontoitem(cs),
			   "Internal sanity check failed for home server %s.",
			   name2);
		free(home);
		free(hs_type);
		hs_type = NULL;
		free(hs_check);
		hs_check = NULL;
		return 0;
	}

	if (!home->port || (home->port > 65535)) {
		cf_log_err(cf_sectiontoitem(cs),
			   "No port, or invalid port defined for home server %s.",
			   name2);
		free(home);
		free(hs_type);
		hs_type = NULL;
		free(hs_check);
		hs_check = NULL;
		return 0;
	}

	if (0) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Fatal error!  Home server %s is ourselves!",
			   name2);
		free(home);
		free(hs_type);
		hs_type = NULL;
		free(hs_check);
		hs_check = NULL;
		return 0;
	}

	/*
	 *	Use a reasonable default.
	 */
	if (!hs_type) hs_type = strdup("auth+acct");

	if (strcasecmp(hs_type, "auth") == 0) {
		home->type = HOME_TYPE_AUTH;
		if (type != home->type) {
			cf_log_err(cf_sectiontoitem(cs),
				   "Server pool of \"acct\" servers cannot include home server %s of type \"auth\"",
				   name2);
			free(home);
			return 0;
		}

	} else if (strcasecmp(hs_type, "acct") == 0) {
		home->type = HOME_TYPE_ACCT;
		if (type != home->type) {
			cf_log_err(cf_sectiontoitem(cs),
				   "Server pool of \"auth\" servers cannot include home server %s of type \"acct\"",
				   name2);
			free(home);
			return 0;
		}

	} else if (strcasecmp(hs_type, "auth+acct") == 0) {
		home->type = HOME_TYPE_AUTH;
		dual = TRUE;

	} else {
		cf_log_err(cf_sectiontoitem(cs),
			   "Invalid type \"%s\" for home server %s.",
			   hs_type, name2);
		free(home);
		free(hs_type);
		hs_type = NULL;
		free(hs_check);
		hs_check = NULL;
		return 0;
	}
	free(hs_type);
	hs_type = NULL;

	if (!home->secret) {
		cf_log_err(cf_sectiontoitem(cs),
			   "No shared secret defined for home server %s.",
			   name2);
		free(home);
		return 0;
	}

	if (!hs_check || (strcasecmp(hs_check, "none") == 0)) {
		home->ping_check = HOME_PING_CHECK_NONE;

	} else if (strcasecmp(hs_check, "status-server") == 0) {
		home->ping_check = HOME_PING_CHECK_STATUS_SERVER;

	} else if (strcasecmp(hs_check, "request") == 0) {
		home->ping_check = HOME_PING_CHECK_REQUEST;

	} else {
		cf_log_err(cf_sectiontoitem(cs),
			   "Invalid ping_check \"%s\" for home server %s.",
			   hs_check, name2);
		free(home);
		free(hs_check);
		hs_check = NULL;
		return 0;
	}
	free(hs_check);
	hs_check = NULL;

	if ((home->ping_check != HOME_PING_CHECK_NONE) &&
	    (home->ping_check != HOME_PING_CHECK_STATUS_SERVER)) {
		if (!home->ping_user_name) {
			cf_log_err(cf_sectiontoitem(cs), "You must supply a user name to enable ping checks");
			free(home);
			return 0;
		}

		if ((home->type == HOME_TYPE_AUTH) &&
		    !home->ping_user_password) {
			cf_log_err(cf_sectiontoitem(cs), "You must supply a password to enable ping checks");
			free(home);
			return 0;
		}
	}

	if (rbtree_finddata(home_servers_byaddr, home)) {
		DEBUG2("Ignoring duplicate home server %s.", name2);
		return 1;
	}

	if (!rbtree_insert(home_servers_byname, home)) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Internal error adding home server %s.",
			   name2);
		free(home);
		return 0;
	}

	if (!rbtree_insert(home_servers_byaddr, home)) {
		rbtree_deletebydata(home_servers_byname, home);
		cf_log_err(cf_sectiontoitem(cs),
			   "Internal error adding home server %s.",
			   name2);
		free(home);
		return 0;
	}

	if (home->response_window < 5) home->response_window = 5;
	if (home->response_window > 60) home->response_window = 60;

	if (home->max_outstanding < 8) home->max_outstanding = 8;
	if (home->max_outstanding > 65536*16) home->max_outstanding = 65536*16;

	if (home->ping_interval < 6) home->ping_interval = 6;
	if (home->ping_interval > 120) home->ping_interval = 120;

	if (home->zombie_period < 20) home->zombie_period = 20;
	if (home->zombie_period > 120) home->zombie_period = 120;

	if (home->zombie_period < home->response_window) {
		home->zombie_period = home->response_window;
	}

	if (home->num_pings_to_alive < 3) home->num_pings_to_alive = 3;
	if (home->num_pings_to_alive > 10) home->num_pings_to_alive = 10;

	if (home->ping_timeout < 3) home->ping_timeout = 3;
	if (home->ping_timeout > 10) home->ping_timeout = 10;

	if (home->revive_interval < 60) home->revive_interval = 60;
	if (home->revive_interval > 3600) home->revive_interval = 3600;

	if (dual) {
		home_server *home2 = rad_malloc(sizeof(*home2));

		memcpy(home2, home, sizeof(*home2));

		home2->type = HOME_TYPE_ACCT;
		home2->port++;
		home2->ping_user_password = NULL;
		home2->cs = cs;

		if (!rbtree_insert(home_servers_byname, home2)) {
			cf_log_err(cf_sectiontoitem(cs),
				   "Internal error adding home server %s.",
				   name2);
			free(home2);
			return 0;
		}
		
		if (!rbtree_insert(home_servers_byaddr, home2)) {
			rbtree_deletebydata(home_servers_byname, home2);
			cf_log_err(cf_sectiontoitem(cs),
				   "Internal error adding home server %s.",
				   name2);
			free(home2);
			return 0;
		}
	}

	return 1;
}


static home_pool_t *server_pool_alloc(const char *name, home_pool_type_t type,
				      int server_type, int num_home_servers)
{
	home_pool_t *pool;

	pool = rad_malloc(sizeof(*pool) + (sizeof(pool->servers[0]) *
					   num_home_servers));
	if (!pool) return NULL;	/* just for pairanoia */
	
	memset(pool, 0, sizeof(*pool) + (sizeof(pool->servers[0]) *
					 num_home_servers));

	pool->name = name;
	pool->type = type;
	pool->server_type = server_type;
	pool->num_home_servers = num_home_servers;

	return pool;
}


static int server_pool_add(realm_config_t *rc,
			   CONF_SECTION *cs, int server_type, int do_print)
{
	const char *name2;
	home_pool_t *pool = NULL;
	const char *value;
	CONF_PAIR *cp;
	int num_home_servers;

	name2 = cf_section_name1(cs);
	if (!name2 || ((strcasecmp(name2, "server_pool") != 0) &&
		       (strcasecmp(name2, "home_server_pool") != 0))) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Section is not a home_server_pool.");
		return 0;
	}

	name2 = cf_section_name2(cs);
	if (!name2) {
		cf_log_err(cf_sectiontoitem(cs),
			   "Server pool section is missing a name.");
		return 0;
	}

	/*
	 *	Count the home servers and initalize them.
	 */
	num_home_servers = 0;
	for (cp = cf_pair_find(cs, "home_server");
	     cp != NULL;
	     cp = cf_pair_find_next(cs, cp, "home_server")) {
		home_server myhome, *home;
		CONF_SECTION *server_cs;

		num_home_servers++;

		value = cf_pair_value(cp);
		if (!value) {
			cf_log_err(cf_pairtoitem(cp),
				   "No value given for home_server.");
			return 0;;
		}

		myhome.name = value;
		myhome.type = server_type;
		home = rbtree_finddata(home_servers_byname, &myhome);
		if (home) continue;

		server_cs = cf_section_sub_find_name2(rc->cs,
						      "home_server",
						      value);
		if (!server_cs) {
			cf_log_err(cf_pairtoitem(cp),
				   "Unknown home_server \"%s\".",
				   value);
			return 0;
		}

		if (!home_server_add(server_cs, server_type)) {
			return 0;
		}

		home = rbtree_finddata(home_servers_byname, &myhome);
		if (!home) {
			radlog(L_ERR, "Internal sanity check failed %d",
			       __LINE__);
			return 0;
		}
	}

	if (num_home_servers == 0) {
		cf_log_err(cf_sectiontoitem(cs),
			   "No home servers defined in pool %s",
			   name2);
		goto error;
	}

	pool = server_pool_alloc(name2, HOME_POOL_FAIL_OVER, server_type,
				 num_home_servers);
	pool->cs = cs;

	if (do_print) cf_log_info(cs, " home_server_pool %s {", name2);

	cp = cf_pair_find(cs, "type");
	if (cp) {
		static FR_NAME_NUMBER pool_types[] = {
			{ "load-balance", HOME_POOL_LOAD_BALANCE },
			{ "fail-over", HOME_POOL_FAIL_OVER },
			{ "round_robin", HOME_POOL_LOAD_BALANCE },
			{ "fail_over", HOME_POOL_FAIL_OVER },
			{ "client-balance", HOME_POOL_CLIENT_BALANCE },
			{ "client-port-balance", HOME_POOL_CLIENT_PORT_BALANCE },
			{ "keyed-balance", HOME_POOL_KEYED_BALANCE },
			{ NULL, 0 }
		};

		value = cf_pair_value(cp);
		if (!value) {
			cf_log_err(cf_pairtoitem(cp),
				   "No value given for type.");
			goto error;
		}

		pool->type = fr_str2int(pool_types, value, 0);
		if (!pool->type) {
			cf_log_err(cf_pairtoitem(cp),
				   "Unknown type \"%s\".",
				   value);
			goto error;
		}

		if (do_print) cf_log_info(cs, "\ttype = %s", value);
	}

	cp = cf_pair_find(cs, "virtual_server");
	if (cp) {
		pool->virtual_server = cf_pair_value(cp);
		if (do_print && pool->virtual_server) {
			cf_log_info(cs, "\tvirtual_server = %s", pool->virtual_server);
		}

		if (!cf_section_sub_find_name2(rc->cs, "server",
					       pool->virtual_server)) {
			cf_log_err(cf_pairtoitem(cp), "No such server %s",
				   pool->virtual_server);
			goto error;
		}

	}

	num_home_servers = 0;
	for (cp = cf_pair_find(cs, "home_server");
	     cp != NULL;
	     cp = cf_pair_find_next(cs, cp, "home_server")) {
		home_server myhome, *home;

		value = cf_pair_value(cp);
		if (!value) {
			cf_log_err(cf_pairtoitem(cp),
				   "No value given for home_server.");
			goto error;
		}

		myhome.name = value;
		myhome.type = server_type;

		home = rbtree_finddata(home_servers_byname, &myhome);
		if (!home) {
			DEBUG2("Internal sanity check failed");
			goto error;
		}

		if (0) {
			DEBUG2("Warning: Duplicate home server %s in server pool %s", home->name, pool->name);
			continue;
		}

		if (do_print) cf_log_info(cs, "\thome_server = %s", home->name);
		pool->servers[num_home_servers++] = home;
	} /* loop over home_server's */

	if (!rbtree_insert(home_pools_byname, pool)) {
		rad_assert("Internal sanity check failed");
		goto error;
	}

	if (do_print) cf_log_info(cs, " }");

	rad_assert(pool->server_type != 0);

	return 1;

 error:
	if (do_print) cf_log_info(cs, " }");
	free(pool);
	return 0;
}


static int old_server_add(realm_config_t *rc, CONF_SECTION *cs,
			  const char *realm,
			  const char *name, const char *secret,
			  home_pool_type_t ldflag, home_pool_t **pool_p,
			  int type)
{
	int i, insert_point, num_home_servers;
	home_server myhome, *home;
	home_pool_t mypool, *pool;
	CONF_SECTION *subcs;

	/*
	 *	LOCAL realms get sanity checked, and nothing else happens.
	 */
	if (strcmp(name, "LOCAL") == 0) {
		if (*pool_p) {
			cf_log_err(cf_sectiontoitem(cs), "Realm \"%s\" cannot be both LOCAL and remote", name);
			return 0;
		}
		return 1;
	}

	mypool.name = realm;
	mypool.server_type = type;
	pool = rbtree_finddata(home_pools_byname, &mypool);
	if (pool) {
		if (pool->type != ldflag) {
			cf_log_err(cf_sectiontoitem(cs), "Inconsistent ldflag for server pool \"%s\"", name);
			return 0;
		}

		if (pool->server_type != type) {
			cf_log_err(cf_sectiontoitem(cs), "Inconsistent home server type for server pool \"%s\"", name);
			return 0;
		}
	}

	myhome.name = name;
	myhome.type = type;
	home = rbtree_finddata(home_servers_byname, &myhome);
	if (home) {
		if (strcmp(home->secret, secret) != 0) {
			cf_log_err(cf_sectiontoitem(cs), "Inconsistent shared secret for home server \"%s\"", name);
			return 0;
		}

		if (home->type != type) {
			cf_log_err(cf_sectiontoitem(cs), "Inconsistent type for home server \"%s\"", name);
			return 0;
		}

		/*
		 *	See if the home server is already listed
		 *	in the pool.  If so, do nothing else.
		 */
		if (pool) for (i = 0; i < pool->num_home_servers; i++) {
			if (pool->servers[i] == home) {
				return 1;
			}
		}
	}

	/*
	 *	If we do have a pool, check that there is room to
	 *	insert the home server we've found, or the one that we
	 *	create here.
	 *
	 *	Note that we insert it into the LAST available
	 *	position, in order to maintain the same order as in
	 *	the configuration files.
	 */
	insert_point = -1;
	if (pool) {
		for (i = pool->num_home_servers - 1; i >= 0; i--) {
			if (pool->servers[i]) break;

			if (!pool->servers[i]) {
				insert_point = i;
			}
		}

		if (insert_point < 0) {
			cf_log_err(cf_sectiontoitem(cs), "No room in pool to add home server \"%s\".  Please update the realm configuration to use the new-style home servers and server pools.", name);
			return 0;
		}
	}

	/*
	 *	No home server, allocate one.
	 */
	if (!home) {
		const char *p;
		char *q;

		home = rad_malloc(sizeof(*home));
		memset(home, 0, sizeof(*home));

		home->name = name;
		home->hostname = name;
		home->type = type;
		home->secret = secret;
		home->cs = cs;

		p = strchr(name, ':');
		if (!p) {
			if (type == HOME_TYPE_AUTH) {
				home->port = PW_AUTH_UDP_PORT;
			} else {
				home->port = PW_ACCT_UDP_PORT;
			}

			p = name;
			q = NULL;

		} else if (p == name) {
				cf_log_err(cf_sectiontoitem(cs),
					   "Invalid hostname %s.",
					   name);
				free(home);
				return 0;

		} else {
			home->port = atoi(p + 1);
			if ((home->port == 0) || (home->port > 65535)) {
				cf_log_err(cf_sectiontoitem(cs),
					   "Invalid port %s.",
					   p + 1);
				free(home);
				return 0;
			}

			q = rad_malloc((p - name) + 1);
			memcpy(q, name, (p - name));
			q[p - name] = '\0';
			p = q;
		}

		if (ip_hton(p, AF_UNSPEC, &home->ipaddr) < 0) {
			cf_log_err(cf_sectiontoitem(cs),
				   "Failed looking up hostname %s.",
				   p);
			free(home);
			free(q);
			return 0;
		}
		free(q);

		/*
		 *	Use the old-style configuration.
		 */
		home->max_outstanding = 65535*16;
		home->zombie_period = rc->retry_delay * rc->retry_count;
		if (home->zombie_period == 0) home->zombie_period =30;
		home->response_window = home->zombie_period - 1;

		home->ping_check = HOME_PING_CHECK_NONE;

		home->revive_interval = rc->dead_time;

		if (rbtree_finddata(home_servers_byaddr, home)) {
			cf_log_err(cf_sectiontoitem(cs), "Home server %s has the same IP address and/or port as another home server.", name);
			free(home);
			return 0;
		}

		if (!rbtree_insert(home_servers_byname, home)) {
			cf_log_err(cf_sectiontoitem(cs), "Internal error adding home server %s.", name);
			free(home);
			return 0;
		}

		if (!rbtree_insert(home_servers_byaddr, home)) {
			rbtree_deletebydata(home_servers_byname, home);
			cf_log_err(cf_sectiontoitem(cs), "Internal error adding home server %s.", name);
			free(home);
			return 0;
		}
	}

	/*
	 *	We now have a home server, see if we can insert it
	 *	into pre-existing pool.
	 */
	if (insert_point >= 0) {
		rad_assert(pool != NULL);
		pool->servers[insert_point] = home;
		return 1;
	}

	rad_assert(pool == NULL);
	rad_assert(home != NULL);

	/*
	 *	Count the old-style realms of this name.
	 */
	num_home_servers = 0;
	for (subcs = cf_section_find_next(cs, NULL, "realm");
	     subcs != NULL;
	     subcs = cf_section_find_next(cs, subcs, "realm")) {
		const char *this = cf_section_name2(subcs);

		if (!this || (strcmp(this, realm) != 0)) continue;
		num_home_servers++;
	}

	if (num_home_servers == 0) {
		cf_log_err(cf_sectiontoitem(cs), "Internal error counting pools for home server %s.", name);
		free(home);
		return 0;
	}

	pool = server_pool_alloc(realm, ldflag, type, num_home_servers);
	pool->cs = cs;

	pool->servers[0] = home;

	if (!rbtree_insert(home_pools_byname, pool)) {
		rad_assert("Internal sanity check failed");
		return 0;
	}

	*pool_p = pool;

	return 1;
}

static int old_realm_config(realm_config_t *rc, CONF_SECTION *cs, REALM *r)
{
	const char *host;
	const char *secret = NULL;
	home_pool_type_t ldflag;
	CONF_PAIR *cp;

	cp = cf_pair_find(cs, "ldflag");
	ldflag = HOME_POOL_FAIL_OVER;
	if (cp) {
		host = cf_pair_value(cp);
		if (!host) {
			cf_log_err(cf_pairtoitem(cp), "No value specified for ldflag");
			return 0;
		}

		if (strcasecmp(host, "fail_over") == 0) {
			cf_log_info(cs, "\tldflag = fail_over");
			
		} else if (strcasecmp(host, "round_robin") == 0) {
			ldflag = HOME_POOL_LOAD_BALANCE;
			cf_log_info(cs, "\tldflag = round_robin");
			
		} else {
			cf_log_err(cf_sectiontoitem(cs), "Unknown value \"%s\" for ldflag", host);
			return 0;
		}
	} /* else don't print it. */

	/*
	 *	Allow old-style if it doesn't exist, or if it exists and
	 *	it's LOCAL.
	 */
	cp = cf_pair_find(cs, "authhost");
	if (cp) {
		host = cf_pair_value(cp);
		if (!host) {
			cf_log_err(cf_pairtoitem(cp), "No value specified for authhost");
			return 0;
		}

		if (strcmp(host, "LOCAL") != 0) {
			cp = cf_pair_find(cs, "secret");
			if (!cp) {
				cf_log_err(cf_sectiontoitem(cs), "No shared secret supplied for realm: %s", r->name);
				return 0;
			}

			secret = cf_pair_value(cp);
			if (!secret) {
				cf_log_err(cf_pairtoitem(cp), "No value specified for secret");
				return 0;
			}
		}
			
		cf_log_info(cs, "\tauthhost = %s",  host);

		if (!old_server_add(rc, cs, r->name, host, secret, ldflag,
				    &r->auth_pool, HOME_TYPE_AUTH)) {
			return 0;
		}
	}

	cp = cf_pair_find(cs, "accthost");
	if (cp) {
		host = cf_pair_value(cp);
		if (!host) {
			cf_log_err(cf_pairtoitem(cp), "No value specified for accthost");
			return 0;
		}

		/*
		 *	Don't look for a secret again if it was found
		 *	above.
		 */
		if ((strcmp(host, "LOCAL") != 0) && !secret) {
			cp = cf_pair_find(cs, "secret");
			if (!cp) {
				cf_log_err(cf_sectiontoitem(cs), "No shared secret supplied for realm: %s", r->name);
				return 0;
			}
			
			secret = cf_pair_value(cp);
			if (!secret) {
				cf_log_err(cf_pairtoitem(cp), "No value specified for secret");
				return 0;
			}
		}
		
		cf_log_info(cs, "\taccthost = %s", host);

		if (!old_server_add(rc, cs, r->name, host, secret, ldflag,
				    &r->acct_pool, HOME_TYPE_ACCT)) {
			return 0;
		}
	}

	if (secret) cf_log_info(cs, "\tsecret = %s", secret);

	return 1;

}


static int add_pool_to_realm(realm_config_t *rc, CONF_SECTION *cs,
			     const char *name, home_pool_t **dest,
			     int server_type, int do_print)
{
	home_pool_t mypool, *pool;

	mypool.name = name;
	mypool.server_type = server_type;

	pool = rbtree_finddata(home_pools_byname, &mypool);
	if (!pool) {
		CONF_SECTION *pool_cs;

		pool_cs = cf_section_sub_find_name2(rc->cs,
						    "home_server_pool",
						    name);
		if (!pool_cs) {
			pool_cs = cf_section_sub_find_name2(rc->cs,
							    "server_pool",
							    name);
		}
		if (!pool_cs) {
			cf_log_err(cf_sectiontoitem(cs), "Failed to find home_server_pool \"%s\"", name);
			return 0;
		}

		if (!server_pool_add(rc, pool_cs, server_type, do_print)) {
			return 0;
		}

		pool = rbtree_finddata(home_pools_byname, &mypool);
		if (!pool) {
			radlog(L_ERR, "Internal sanity check failed in add_pool_to_realm");
			return 0;
		}
	}

	if (pool->server_type != server_type) {
		cf_log_err(cf_sectiontoitem(cs), "Incompatible home_server_pool \"%s\" (mixed auth_pool / acct_pool)", name);
		return 0;
	}

	*dest = pool;

	return 1;
}

static int realm_add(realm_config_t *rc, CONF_SECTION *cs)
{
	const char *name2;
	REALM *r = NULL;
	CONF_PAIR *cp;
	home_pool_t *auth_pool, *acct_pool;
	const char *auth_pool_name, *acct_pool_name;

	name2 = cf_section_name1(cs);
	if (!name2 || (strcasecmp(name2, "realm") != 0)) {
		cf_log_err(cf_sectiontoitem(cs), "Section is not a realm.");
		return 0;
	}

	name2 = cf_section_name2(cs);
	if (!name2) {
		cf_log_err(cf_sectiontoitem(cs), "Realm section is missing the realm name.");
		return 0;
	}

	auth_pool = acct_pool = NULL;
	auth_pool_name = acct_pool_name = NULL;

	/*
	 *	Prefer new configuration to old one.
	 */
	cp = cf_pair_find(cs, "pool");
	if (!cp) cp = cf_pair_find(cs, "home_server_pool");
	if (cp) auth_pool_name = cf_pair_value(cp);
	if (cp && auth_pool_name) {
		acct_pool_name = auth_pool_name;
		if (!add_pool_to_realm(rc, cs,
				       auth_pool_name, &auth_pool,
				       HOME_TYPE_AUTH, 1)) {
			return 0;
		}
		if (!add_pool_to_realm(rc, cs,
				       auth_pool_name, &acct_pool,
				       HOME_TYPE_ACCT, 0)) {
			return 0;
		}
	}

	cp = cf_pair_find(cs, "auth_pool");
	if (cp) auth_pool_name = cf_pair_value(cp);
	if (cp && auth_pool_name) {
		if (auth_pool) {
			cf_log_err(cf_sectiontoitem(cs), "Cannot use \"pool\" and \"auth_pool\" at the same time.");
			return 0;
		}
		if (!add_pool_to_realm(rc, cs,
				       auth_pool_name, &auth_pool,
				       HOME_TYPE_AUTH, 1)) {
			return 0;
		}
	}

	cp = cf_pair_find(cs, "acct_pool");
	if (cp) acct_pool_name = cf_pair_value(cp);
	if (cp && acct_pool_name) {
		int do_print = TRUE;

		if (acct_pool) {
			cf_log_err(cf_sectiontoitem(cs), "Cannot use \"pool\" and \"acct_pool\" at the same time.");
			return 0;
		}

		if (!auth_pool ||
		    (strcmp(auth_pool_name, acct_pool_name) != 0)) {
			do_print = TRUE;
		}

		if (!add_pool_to_realm(rc, cs,
				       acct_pool_name, &acct_pool,
				       HOME_TYPE_ACCT, do_print)) {
			return 0;
		}
	}

	cf_log_info(cs, " realm %s {", name2);

	/*
	 *	The realm MAY already exist if it's an old-style realm.
	 *	In that case, merge the old-style realm with this one.
	 */
	r = realm_find(name2);
	if (r) {
		if (cf_pair_find(cs, "auth_pool") ||
		    cf_pair_find(cs, "acct_pool")) {
			cf_log_err(cf_sectiontoitem(cs), "Duplicate realm \"%s\"", name2);
			goto error;
		}

		if (!old_realm_config(rc, cs, r)) {
			goto error;
		}

		cf_log_info(cs, " } # realm %s", name2);
		return 1;
	}

	r = rad_malloc(sizeof(*r));
	memset(r, 0, sizeof(*r));

	r->name = name2;
	r->auth_pool = auth_pool;
	r->acct_pool = acct_pool;
	r->striprealm = 1;

	if (auth_pool_name &&
	    (auth_pool_name == acct_pool_name)) { /* yes, ptr comparison */
		cf_log_info(cs, "\tpool = %s", auth_pool_name);
	} else {
		if (auth_pool_name) cf_log_info(cs, "\tauth_pool = %s", auth_pool_name);
		if (acct_pool_name) cf_log_info(cs, "\tacct_pool = %s", acct_pool_name);
	}

	cp = cf_pair_find(cs, "nostrip");
	if (cp && (cf_pair_value(cp) == NULL)) {
		r->striprealm = 0;
		cf_log_info(cs, "\tnostrip");
	}

	/*
	 *	We're a new-style realm.  Complain if we see the old
	 *	directives.
	 */
	if (r->auth_pool || r->acct_pool) {
		if (((cp = cf_pair_find(cs, "authhost")) != NULL) ||
		    ((cp = cf_pair_find(cs, "accthost")) != NULL) ||
		    ((cp = cf_pair_find(cs, "secret")) != NULL) ||
		    ((cp = cf_pair_find(cs, "ldflag")) != NULL)) {
			DEBUG2("WARNING: Ignoring old-style configuration entry \"%s\" in realm \"%s\"", cf_pair_attr(cp), r->name);
		}


		/*
		 *	The realm MAY be an old-style realm, as there
		 *	was no auth_pool or acct_pool.  Double-check
		 *	it, just to be safe.
		 */
	} else if (!old_realm_config(rc, cs, r)) {
		goto error;
	}

	if (!rbtree_insert(realms_byname, r)) {
		rad_assert("Internal sanity check failed");
		goto error;
	}

	cf_log_info(cs, " }");

	return 1;

 error:
	cf_log_info(cs, " } # realm %s", name2);
	free(r);
	return 0;
}


int realms_init(CONF_SECTION *config)
{
	CONF_SECTION *cs;
	realm_config_t *rc, *old_rc;

	if (realms_byname) return 1;

	realms_byname = rbtree_create(realm_name_cmp, free, 0);
	if (!realms_byname) {
		realms_free();
		return 0;
	}

	home_servers_byaddr = rbtree_create(home_server_addr_cmp, free, 0);
	if (!home_servers_byaddr) {
		realms_free();
		return 0;
	}

	home_servers_byname = rbtree_create(home_server_name_cmp, NULL, 0);
	if (!home_servers_byname) {
		realms_free();
		return 0;
	}

	home_pools_byname = rbtree_create(home_pool_name_cmp, free, 0);
	if (!home_pools_byname) {
		realms_free();
		return 0;
	}

	rc = rad_malloc(sizeof(*rc));
	memset(rc, 0, sizeof(*rc));
	rc->cs = config;

	cs = cf_subsection_find_next(config, NULL, "proxy");
	if (cs) {
		cf_section_parse(cs, rc, proxy_config);
	} else {
		rc->dead_time = DEAD_TIME;
		rc->retry_count = RETRY_COUNT;
		rc->retry_delay = RETRY_DELAY;
		rc->fallback = 0;
		rc->wake_all_if_all_dead= 0;
	}

	for (cs = cf_subsection_find_next(config, NULL, "realm");
	     cs != NULL;
	     cs = cf_subsection_find_next(config, cs, "realm")) {
		if (!realm_add(rc, cs)) {
			free(rc);
			realms_free();
			return 0;
		}
	}

	xlat_register("home_server", xlat_home_server, NULL);
	xlat_register("home_server_pool", xlat_server_pool, NULL);

	/*
	 *	Swap pointers atomically.
	 */
	old_rc = realm_config;
	realm_config = rc;
	free(old_rc);

	return 1;
}


/*
 *	Find a realm in the REALM list.
 */
REALM *realm_find(const char *name)
{
	REALM myrealm;
	REALM *realm;
	
	if (!name) name = "NULL";

	myrealm.name = name;
	realm = rbtree_finddata(realms_byname, &myrealm);
	if (realm) return realm;

	/*
	 *	Couldn't find a realm.  Look for DEFAULT.
	 */
	myrealm.name = "DEFAULT";
	return rbtree_finddata(realms_byname, &myrealm);
}


home_server *home_server_ldb(const char *realmname,
			     home_pool_t *pool, REQUEST *request)
{
	int		start;
	int		count;
	home_server	*found = NULL;
	VALUE_PAIR	*vp;

	start = 0;

	/*
	 *	Determine how to pick choose the home server.
	 */
	switch (pool->type) {
		uint32_t hash;

		/*
		 *	For load-balancing by client IP address, we
		 *	pick a home server by hashing the client IP.
		 *
		 *	This isn't as even a load distribution as
		 *	tracking the State attribute, but it's better
		 *	than nothing.
		 */
	case HOME_POOL_CLIENT_BALANCE:
		switch (request->packet->src_ipaddr.af) {
		case AF_INET:
			hash = fr_hash(&request->packet->src_ipaddr.ipaddr.ip4addr,
					 sizeof(request->packet->src_ipaddr.ipaddr.ip4addr));
			break;
		case AF_INET6:
			hash = fr_hash(&request->packet->src_ipaddr.ipaddr.ip6addr,
					 sizeof(request->packet->src_ipaddr.ipaddr.ip6addr));
			break;
		default:
			hash = 0;
			break;
		}
		start = hash % pool->num_home_servers;
		break;

	case HOME_POOL_CLIENT_PORT_BALANCE:
		switch (request->packet->src_ipaddr.af) {
		case AF_INET:
			hash = fr_hash(&request->packet->src_ipaddr.ipaddr.ip4addr,
					 sizeof(request->packet->src_ipaddr.ipaddr.ip4addr));
			break;
		case AF_INET6:
			hash = fr_hash(&request->packet->src_ipaddr.ipaddr.ip6addr,
					 sizeof(request->packet->src_ipaddr.ipaddr.ip6addr));
			break;
		default:
			hash = 0;
			break;
		}
		fr_hash_update(&request->packet->src_port,
				 sizeof(request->packet->src_port), hash);
		start = hash % pool->num_home_servers;
		break;

	case HOME_POOL_KEYED_BALANCE:
		if ((vp = pairfind(request->config_items, PW_LOAD_BALANCE_KEY)) != NULL) {
			hash = fr_hash(vp->vp_strvalue, vp->length);
			start = hash % pool->num_home_servers;
			break;
		}
		/* FALL-THROUGH */
				
	case HOME_POOL_LOAD_BALANCE:
		found = pool->servers[0];

	default:
		start = 0;
		break;
	}

	/*
	 *	Starting with the home server we chose, loop through
	 *	all home servers.  If the current one is dead, skip
	 *	it.  If it is too busy, skip it.
	 *
	 *	Otherwise, use it.
	 */
	for (count = 0; count < pool->num_home_servers; count++) {
		home_server *home = pool->servers[(start + count) % pool->num_home_servers];

		if (home->state == HOME_STATE_IS_DEAD) {
			continue;
		}

		/*
		 *	This home server is too busy.  Choose another one.
		 */
		if (home->currently_outstanding >= home->max_outstanding) {
			continue;
		}

		if (pool->type != HOME_POOL_LOAD_BALANCE) {
			return home;
		}

		DEBUG3("PROXY %s %d\t%s %d",
		       found->name, found->currently_outstanding,
		       home->name, home->currently_outstanding);

		/*
		 *	Prefer this server if it's less busy than the
		 *	one we previously found.
		 */
		if (home->currently_outstanding < found->currently_outstanding) {
			DEBUG3("Choosing %s: It's less busy than %s",
			       home->name, found->name);
			found = home;
			continue;
		}

		/*
		 *	Ignore servers which are busier than the one
		 *	we found.
		 */
		if (home->currently_outstanding > found->currently_outstanding) {
			DEBUG3("Skipping %s: It's busier than %s",
			       home->name, found->name);
			continue;
		}

		if (home->total_requests_sent < found->total_requests_sent) {
			DEBUG3("Choosing %s: It's been less busy than %s",
			       home->name, found->name);
			found = home;
			continue;
		}

		if (home->total_requests_sent > found->total_requests_sent) {
			DEBUG3("Skipping %s: It's been busier than %s",
			       home->name, found->name);
			continue;
		}

		/*
		 *	From the list of servers which have the same
		 *	load, choose one at random.
		 */
		if (((count + 1) * (fr_rand() & 0xffff)) < (uint32_t) 0x10000) {
			found = home;
		}

	} /* loop over the home servers */

	if (found) return found;

	/*
	 *	No live match found, and no fallback to the "DEFAULT"
	 *	realm.  We fix this by blindly marking all servers as
	 *	"live".  But only do it for ones that don't support
	 *	"pings", as they will be marked live when they
	 *	actually are live.
	 */
	if (!realm_config->fallback &&
	    realm_config->wake_all_if_all_dead) {
		home_server *lb = NULL;

		for (count = 0; count < pool->num_home_servers; count++) {
			home_server *home = pool->servers[count];

			if ((home->state == HOME_STATE_IS_DEAD) &&
			    (home->ping_check == HOME_PING_CHECK_NONE)) {
				home->state = HOME_STATE_ALIVE;
				if (!lb) lb = home;
			}
		}

		if (lb) return lb;
	}

	/*
	 *	Still nothing.  Look up the DEFAULT realm, but only
	 *	if we weren't looking up the NULL or DEFAULT realms.
	 */
	if (realm_config->fallback &&
	    realmname &&
	    (strcmp(realmname, "NULL") != 0) &&
	    (strcmp(realmname, "DEFAULT") != 0)) {
		REALM *rd = realm_find("DEFAULT");

		if (!rd) return NULL;

		pool = NULL;
		if (request->packet->code == PW_AUTHENTICATION_REQUEST) {
			pool = rd->auth_pool;

		} else if (request->packet->code == PW_ACCOUNTING_REQUEST) {
			pool = rd->acct_pool;
		}
		if (!pool) return NULL;

		DEBUG2("  Realm %s has no live home servers.  Falling back to the DEFAULT realm.", realmname);
		return home_server_ldb(rd->name, pool, request);
	}

	/*
	 *	Still haven't found anything.  Oh well.
	 */
	return NULL;
}


home_server *home_server_find(fr_ipaddr_t *ipaddr, int port)
{
	home_server myhome;

	myhome.ipaddr = *ipaddr;
	myhome.port = port;

	return rbtree_finddata(home_servers_byaddr, &myhome);
}
