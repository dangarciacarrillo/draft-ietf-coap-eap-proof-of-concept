/*
 * evaluate.c	Evaluate complex conditions
 *
 * Version:	$Id: evaluate.c,v 1.37 2008/01/15 16:31:08 aland Exp $
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
RCSID("$Id: evaluate.c,v 1.37 2008/01/15 16:31:08 aland Exp $")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <ctype.h>

#ifdef HAVE_REGEX_H
#include <regex.h>

/*
 *  For POSIX Regular expressions.
 *  (0) Means no extended regular expressions.
 *  REG_EXTENDED means use extended regular expressions.
 */
#ifndef REG_EXTENDED
#define REG_EXTENDED (0)
#endif

#ifndef REG_NOSUB
#define REG_NOSUB (0)
#endif
#endif


static int all_digits(const char *string)
{
	const char *p = string;

	if (*p == '-') p++;

	while (isdigit((int) *p)) p++;

	return (*p == '\0');
}

#ifndef NDEBUG
#ifndef DEBUG4
#define DEBUG4  if (debug_flag > 4)log_debug
#endif
#else
#define DEBUG4 if (0) log_debug
#endif

static const char *filler = "????????????????????????????????????????????????????????????????";

static const char *expand_string(char *buffer, size_t sizeof_buffer,
				 REQUEST *request,
				 FR_TOKEN value_type, const char *value)
{
	int result;
	char *p;

	switch (value_type) {
	default:
	case T_BARE_WORD:
	case T_SINGLE_QUOTED_STRING:
		return value;

	case T_BACK_QUOTED_STRING:
		result = radius_exec_program(value, request, 1,
					     buffer, sizeof_buffer, NULL,
					     NULL, 0);
		if (result != 0) {
			return NULL;
		}

		/*
		 *	The result should be ASCII.
		 */
		for (p = buffer; *p != '\0'; p++) {
			if (*p < ' ' ) {
				*p = '\0';
				return buffer;
			}
		}
		return buffer;

	case T_DOUBLE_QUOTED_STRING:
		if (!strchr(value, '%')) return value;

		radius_xlat(buffer, sizeof_buffer, value, request, NULL);
		return buffer;
	}

	return NULL;
}

#ifdef HAVE_REGEX_H
static FR_TOKEN getregex(const char **ptr, char *buffer, size_t buflen,
			 int *pcflags)
{
	const char *p = *ptr;
	char *q = buffer;

	if (*p != '/') return T_OP_INVALID;

	*pcflags = REG_EXTENDED;

	p++;
	while (*p) {
		if (buflen <= 1) break;

		if (*p == '/') {
			p++;

			/*
			 *	Check for case insensitivity
			 */
			if (*p == 'i') {
				p++;
				*pcflags |= REG_ICASE;
			}

			break;
		}

		if (*p == '\\') {
			int x;
			
			switch (p[1]) {
			case 'r':
				*q++ = '\r';
				break;
			case 'n':
				*q++ = '\n';
				break;
			case 't':
				*q++ = '\t';
				break;
			case '"':
				*q++ = '"';
				break;
			case '\'':
				*q++ = '\'';
				break;
			case '`':
				*q++ = '`';
				break;
				
				/*
				 *	FIXME: add 'x' and 'u'
				 */

			default:
				if ((p[1] >= '0') && (p[1] <= '9') &&
				    (sscanf(p, "%3o", &x) == 1)) {
					*q++ = x;
					p += 2;
				} else {
					*q++ = p[1];
				}
				break;
			}
			p += 2;
			buflen--;
			continue;
		}

		*(q++) = *(p++);
		buflen--;
	}
	*q = '\0';
	*ptr = p;

	return T_DOUBLE_QUOTED_STRING;
}
#endif

static const FR_NAME_NUMBER modreturn_table[] = {
	{ "reject",     RLM_MODULE_REJECT       },
	{ "fail",       RLM_MODULE_FAIL         },
	{ "ok",         RLM_MODULE_OK           },
	{ "handled",    RLM_MODULE_HANDLED      },
	{ "invalid",    RLM_MODULE_INVALID      },
	{ "userlock",   RLM_MODULE_USERLOCK     },
	{ "notfound",   RLM_MODULE_NOTFOUND     },
	{ "noop",       RLM_MODULE_NOOP         },
	{ "updated",    RLM_MODULE_UPDATED      },
	{ NULL, 0 }
};


static int radius_get_vp(REQUEST *request, const char *name, VALUE_PAIR **vp_p)
			
{
	const char *vp_name = name;
	REQUEST *myrequest = request;
	DICT_ATTR *da;
	VALUE_PAIR *vps = NULL;

	*vp_p = NULL;

	/*
	 *	Allow for tunneled sessions.
	 */
	if (memcmp(vp_name, "outer.", 6) == 0) {
		if (!myrequest->parent) return TRUE;
		vp_name += 6;
		myrequest = myrequest->parent;
	}

	if (memcmp(vp_name, "request:", 8) == 0) {
		vp_name += 8;
		vps = myrequest->packet->vps;

	} else if (memcmp(vp_name, "reply:", 6) == 0) {
		vp_name += 6;
		vps = myrequest->reply->vps;

	} else if (memcmp(vp_name, "proxy-request:", 14) == 0) {
		vp_name += 14;
		if (request->proxy) vps = myrequest->proxy->vps;

	} else if (memcmp(vp_name, "proxy-reply:", 12) == 0) {
		vp_name += 12;
		if (request->proxy_reply) vps = myrequest->proxy_reply->vps;

	} else if (memcmp(vp_name, "config:", 7) == 0) {
		vp_name += 7;
		vps = myrequest->config_items;

	} else if (memcmp(vp_name, "control:", 8) == 0) {
		vp_name += 8;
		vps = myrequest->config_items;

	} else {
		vps = myrequest->packet->vps;
	}

	da = dict_attrbyname(vp_name);
	if (!da) return FALSE;	/* not a dictionary name */

	/*
	 *	May not may not be found, but it *is* a known name.
	 */
	*vp_p = pairfind(vps, da->attr);
	return TRUE;
}


/*
 *	*presult is "did comparison match or not"
 */
static int radius_do_cmp(REQUEST *request, int *presult,
			 FR_TOKEN lt, const char *pleft, FR_TOKEN token,
			 FR_TOKEN rt, const char *pright,
			 int cflags, int modreturn)
{
	int result;
	int lint, rint;
	VALUE_PAIR *vp = NULL;
	char buffer[1024];

	rt = rt;		/* -Wunused */

	if (lt == T_BARE_WORD) {
		/*
		 *	Maybe check the last return code.
		 */
		if (token == T_OP_CMP_TRUE) {
			int isreturn;

			/*
			 *	Looks like a return code, treat is as such.
			 */
			isreturn = fr_str2int(modreturn_table, pleft, -1);
			if (isreturn != -1) {
				*presult = (modreturn == isreturn);
				return TRUE;
			}
		}

		/*
		 *	Bare words on the left can be attribute names.
		 */
		if (radius_get_vp(request, pleft, &vp)) {
			VALUE_PAIR myvp;

			/*
			 *	VP exists, and that's all we're looking for.
			 */
			if (token == T_OP_CMP_TRUE) {
				*presult = (vp != NULL);
				return TRUE;
			}

			if (!vp) {
				DEBUG2("    (Attribute %s was not found)",
				       pleft);
				return FALSE;
			}

#ifdef HAVE_REGEX_H
			/*
			 * 	Regex comparisons treat everything as
			 *	strings.
			 */
			if ((token == T_OP_REG_EQ) ||
			    (token == T_OP_REG_NE)) {
				vp_prints_value(buffer, sizeof(buffer), vp, 0);
				pleft = buffer;
				goto do_checks;
			}
#endif

			memcpy(&myvp, vp, sizeof(myvp));
			if (!pairparsevalue(&myvp, pright)) {
				DEBUG2("Failed parsing \"%s\": %s",
				       pright, librad_errstr);
				return FALSE;
			}

			myvp.operator = token;
			*presult = paircmp(&myvp, vp);
			return TRUE;
		} /* else it's not a attribute in the dictionary */
	}

	do_checks:
	switch (token) {
	case T_OP_GE:
	case T_OP_GT:
	case T_OP_LE:
	case T_OP_LT:
		if (!all_digits(pright)) {
			DEBUG2("    (Right field is not a number at: %s)", pright);
			return FALSE;
		}
		rint = atoi(pright);
		if (!all_digits(pleft)) {
			DEBUG2("    (Left field is not a number at: %s)", pleft);
			return FALSE;
		}
		lint = atoi(pleft);
		break;
		
	default:
		lint = rint = 0;  /* quiet the compiler */
		break;
	}
	
	switch (token) {
	case T_OP_CMP_TRUE:
		/*
		 *	Check for truth or falsehood.
		 */
		if (all_digits(pleft)) {
			lint = atoi(pleft);
			result = (lint != 0);
			
		} else {
			result = (*pleft != '\0');
		}
		break;
		

	case T_OP_CMP_EQ:
		result = (strcmp(pleft, pright) == 0);
		break;
		
	case T_OP_NE:
		result = (strcmp(pleft, pright) != 0);
		break;
		
	case T_OP_GE:
		result = (lint >= rint);
		break;
		
	case T_OP_GT:
		result = (lint > rint);
		break;
		
	case T_OP_LE:
		result = (lint <= rint);
		break;
		
	case T_OP_LT:
		result = (lint < rint);
		break;

#ifdef HAVE_REGEX_H
	case T_OP_REG_EQ: {
		int i, compare;
		regex_t reg;
		regmatch_t rxmatch[REQUEST_MAX_REGEX + 1];
		
		/*
		 *	Include substring matches.
		 */
		regcomp(&reg, pright, cflags);
		compare = regexec(&reg, pleft,
				  REQUEST_MAX_REGEX + 1,
				  rxmatch, 0);
		regfree(&reg);
		
		/*
		 *	Add new %{0}, %{1}, etc.
		 */
		if (compare == 0) for (i = 0; i <= REQUEST_MAX_REGEX; i++) {
			char *r;

			free(request_data_get(request, request,
					      REQUEST_DATA_REGEX | i));
			/*
			 *	No %{i}, skip it.
			 *	We MAY have %{2} without %{1}.
			 */
			if (rxmatch[i].rm_so == -1) continue;
			
			/*
			 *	Copy substring into buffer.
			 */
			memcpy(buffer, pleft + rxmatch[i].rm_so,
			       rxmatch[i].rm_eo - rxmatch[i].rm_so);
			buffer[rxmatch[i].rm_eo - rxmatch[i].rm_so] = '\0';
			
			/*
			 *	Copy substring, and add it to
			 *	the request.
			 *
			 *	Note that we don't check
			 *	for out of memory, which is
			 *	the only error we can get...
			 */
			r = strdup(buffer);
			request_data_add(request, request,
					 REQUEST_DATA_REGEX | i,
					 r, free);
		}
		result = (compare == 0);
	}
		break;
		
	case T_OP_REG_NE: {
		int compare;
		regex_t reg;
		regmatch_t rxmatch[REQUEST_MAX_REGEX + 1];
		
		/*
		 *	Include substring matches.
		 */
		regcomp(&reg, pright, cflags);
		compare = regexec(&reg, pleft,
				  REQUEST_MAX_REGEX + 1,
				  rxmatch, 0);
		regfree(&reg);
		
		result = (compare != 0);
	}
		break;
#endif
		
	default:
		DEBUG4(">>> NOT IMPLEMENTED %d", token);
		result = FALSE;
		break;
	}
	
	*presult = result;
	return TRUE;
}

int radius_evaluate_condition(REQUEST *request, int modreturn, int depth,
			      const char **ptr, int evaluate_it, int *presult)
{
	int found_condition = FALSE;
	int result = TRUE;
	int invert = FALSE;
	int evaluate_next_condition = evaluate_it;
	const char *p = *ptr;
	const char *q, *start;
	FR_TOKEN token, lt, rt;
	char left[1024], right[1024], comp[4];
	const char *pleft, *pright;
	char  xleft[1024], xright[1024];
#ifdef HAVE_REGEX_H
	int cflags = 0;
#endif

	if (!ptr || !*ptr || (depth >= 64)) {
		radlog(L_ERR, "Internal sanity check failed in evaluate condition");
		return FALSE;
	}

	while (*p) {
		while ((*p == ' ') || (*p == '\t')) p++;

		if (*p == '!') {
			DEBUG4(">>> INVERT");
			invert = TRUE;
			p++;
		}

		/*
		 *	It's a subcondition.
		 */
		if (*p == '(') {
			const char *end = p + 1;

			/*
			 *	Evaluate the condition, bailing out on
			 *	parse error.
			 */
			DEBUG4(">>> CALLING EVALUATE %s", end);
			if (!radius_evaluate_condition(request, modreturn,
						       depth + 1, &end,
						       evaluate_next_condition,
						       &result)) {
				return FALSE;
			}

			if (invert && evaluate_next_condition) {
				DEBUG2("%.*s Converting !%s -> %s",
				       depth, filler,
				       (result != FALSE) ? "TRUE" : "FALSE",
				       (result == FALSE) ? "TRUE" : "FALSE");

				       
				result = (result == FALSE);
				invert = FALSE;
			}

			/*
			 *	Start from the end of the previous
			 *	condition
			 */
			p = end;
			DEBUG4(">>> EVALUATE RETURNED ::%s::", end);
			
			if (!((*p == ')') || (*p == '!') ||
			      ((p[0] == '&') && (p[1] == '&')) ||
			      ((p[0] == '|') && (p[1] == '|')))) {

				radlog(L_ERR, "Parse error in condition at: %s", p);
				return FALSE;
			}
			if (*p == ')') p++;	/* skip it */
			found_condition = TRUE;
			
			while ((*p == ' ') || (*p == '\t')) p++;

			/*
			 *	EOL.  It's OK.
			 */
			if (!*p) {
				DEBUG4(">>> AT EOL");
				*ptr = p;
				*presult = result;
				return TRUE;
				
				/*
				 *	(A && B) means "evaluate B
				 *	only if A was true"
				 */
			} else if ((p[0] == '&') && (p[1] == '&')) {
				if (result == TRUE) {
					evaluate_next_condition = evaluate_it;
				} else {
					evaluate_next_condition = FALSE;
				}
				p += 2;
				
				/*
				 *	(A || B) means "evaluate B
				 *	only if A was false"
				 */
			} else if ((p[0] == '|') && (p[1] == '|')) {
				if (result == FALSE) {
					evaluate_next_condition = evaluate_it;
				} else {
					evaluate_next_condition = FALSE;
				}
				p += 2;

			} else if (*p == ')') {
				DEBUG4(">>> CLOSING BRACE");
				*ptr = p;
				*presult = result;
				return TRUE;

			} else {
				/*
				 *	Parse error
				 */
				radlog(L_ERR, "Unexpected trailing text at: %s", p);
				return FALSE;
			}
		} /* else it wasn't an opening brace */

		while ((*p == ' ') || (*p == '\t')) p++;

		/*
		 *	More conditions, keep going.
		 */
		if ((*p == '(') || (p[0] == '!')) continue;

		DEBUG4(">>> LOOKING AT %s", p);
		start = p;

		/*
		 *	Look for common errors.
		 */
		if ((p[0] == '%') && (p[1] == '{')) {
			radlog(L_ERR, "Bare %%{...} is invalid in condition at: %s", p);
			return FALSE;
		}

		/*
		 *	Look for word == value
		 */
		lt = gettoken(&p, left, sizeof(left));
		if ((lt != T_BARE_WORD) &&
		    (lt != T_DOUBLE_QUOTED_STRING) &&
		    (lt != T_SINGLE_QUOTED_STRING) &&
		    (lt != T_BACK_QUOTED_STRING)) {
			radlog(L_ERR, "Expected string or numbers at: %s", p);
			return FALSE;
		}

		pleft = left;
		if (evaluate_next_condition) {
			pleft = expand_string(xleft, sizeof(xleft), request,
					      lt, left);
			if (!pleft) {
				radlog(L_ERR, "Failed expanding string at: %s",
				       left);
				return FALSE;
			}
		}

		/*
		 *	Peek ahead.  Maybe it's just a check for
		 *	existence.  If so, there shouldn't be anything
		 *	else.
		 */
		q = p;
		while ((*q == ' ') || (*q == '\t')) q++;

		/*
		 *	End of condition, 
		 */
		if (!*q || (*q == ')') ||
		    ((*q == '!') && (q[1] != '=') && (q[1] != '~')) ||
		    ((q[0] == '&') && (q[1] == '&')) ||
		    ((q[0] == '|') && (q[1] == '|'))) {
			/*
			 *	Simplify the code.
			 */
			token = T_OP_CMP_TRUE;
			rt = T_OP_INVALID;
			pright = NULL;
			goto do_cmp;
		}

		/*
		 *	Else it's a full "foo == bar" thingy.
		 */
		token = gettoken(&p, comp, sizeof(comp));
		if ((token < T_OP_NE) || (token > T_OP_CMP_EQ) ||
		    (token == T_OP_CMP_TRUE) ||
		    (token == T_OP_CMP_FALSE)) {
			radlog(L_ERR, "Expected comparison at: %s", comp);
			return FALSE;
		}
		
		/*
		 *	Look for common errors.
		 */
		if ((p[0] == '%') && (p[1] == '{')) {
			radlog(L_ERR, "Bare %%{...} is invalid in condition at: %s", p);
			return FALSE;
		}
		
		/*
		 *	Validate strings.
		 */
#ifdef HAVE_REGEX_H
		if ((token == T_OP_REG_EQ) ||
		    (token == T_OP_REG_NE)) {
			rt = getregex(&p, right, sizeof(right), &cflags);
			if (rt != T_DOUBLE_QUOTED_STRING) {
				radlog(L_ERR, "Expected regular expression at: %s", p);
				return FALSE;
			}
		} else
#endif
			rt = gettoken(&p, right, sizeof(right));

		if ((rt != T_BARE_WORD) &&
		    (rt != T_DOUBLE_QUOTED_STRING) &&
		    (rt != T_SINGLE_QUOTED_STRING) &&
		    (rt != T_BACK_QUOTED_STRING)) {
			radlog(L_ERR, "Expected string or numbers at: %s", p);
			return FALSE;
		}
		
		pright = right;
		if (evaluate_next_condition) {
			pright = expand_string(xright, sizeof(xright), request,
					       rt, right);
			if (!pright) {
				radlog(L_ERR, "Failed expanding string at: %s",
				       right);
				return FALSE;
			}
		}
		
		DEBUG4(">>> %d:%s %d %d:%s",
		       lt, pleft, token, rt, pright);
		
	do_cmp:
		if (evaluate_next_condition) {
			/*
			 *	More parse errors.
			 */
			if (!radius_do_cmp(request, &result, lt, pleft, token,
					   rt, pright, cflags, modreturn)) {
				return FALSE;
			}

			DEBUG2("%.*s Evaluating %s(%.*s) -> %s",
			       depth, filler,
			       invert ? "!" : "", p - start, start,
			       (result != FALSE) ? "TRUE" : "FALSE");

			DEBUG4(">>> GOT result %d", result);

			/*
			 *	Not evaluating it.  We may be just
			 *	parsing it.
			 */
		} else if (request) {
			DEBUG2("%.*s Skipping %s(%.*s)",
			       depth, filler,
			       invert ? "!" : "", p - start, start);
		}

		if (invert) {
			DEBUG4(">>> INVERTING result");
			result = (result == FALSE);
			invert = FALSE;
		}

		/*
		 *	Don't evaluate it.
		 */
		DEBUG4(">>> EVALUATE %d ::%s::",
			evaluate_next_condition, p);

		while ((*p == ' ') || (*p == '\t')) p++;

		/*
		 *	Closing brace or EOL, return.
		 */
		if (!*p || (*p == ')') ||
		    ((*p == '!') && (p[1] != '=') && (p[1] != '~')) ||
		    ((p[0] == '&') && (p[1] == '&')) ||
		    ((p[0] == '|') && (p[1] == '|'))) {
			DEBUG4(">>> AT EOL2a");
			*ptr = p;
			if (evaluate_next_condition) *presult = result;
			return TRUE;
		}
	} /* loop over the input condition */

	DEBUG4(">>> AT EOL2b");
	*ptr = p;
	if (evaluate_next_condition) *presult = result;
	return TRUE;
}

/*
 *	The pairmove() function in src/lib/valuepair.c does all sorts of
 *	extra magic that we don't want here.
 *
 *	FIXME: integrate this with the code calling it, so that we
 *	only paircopy() those attributes that we're really going to
 *	use.
 */
static void my_pairmove(REQUEST *request, VALUE_PAIR **to, VALUE_PAIR *from)
{
	int i, j, count, from_count, to_count, tailto;
	VALUE_PAIR *vp, *next, **last;
	VALUE_PAIR **from_list, **to_list;
	int *edited = NULL;

	/*
	 *	Set up arrays for editing, to remove some of the
	 *	O(N^2) dependencies.  This also makes it easier to
	 *	insert and remove attributes.
	 *
	 *	It also means that the operators apply ONLY to the
	 *	attributes in the original list.  With the previous
	 *	implementation of pairmove(), adding two attributes
	 *	via "+=" and then "=" would mean that the second one
	 *	wasn't added, because of the existence of the first
	 *	one in the "to" list.  This implementation doesn't
	 *	have that bug.
	 *
	 *	Also, the previous implementation did NOT implement
	 *	"-=" correctly.  If two of the same attributes existed
	 *	in the "to" list, and you tried to subtract something
	 *	matching the *second* value, then the pairdelete()
	 *	function was called, and the *all* attributes of that
	 *	number were deleted.  With this implementation, only
	 *	the matching attributes are deleted.
	 */
	count = 0;
	for (vp = from; vp != NULL; vp = vp->next) count++;
	from_list = rad_malloc(sizeof(*from_list) * count);

	for (vp = *to; vp != NULL; vp = vp->next) count++;
	to_list = rad_malloc(sizeof(*to_list) * count);

	/*
	 *	Move the lists to the arrays, and break the list
	 *	chains.
	 */
	from_count = 0;
	for (vp = from; vp != NULL; vp = next) {
		next = vp->next;
		from_list[from_count++] = vp;
		vp->next = NULL;
	}

	to_count = 0;
	for (vp = *to; vp != NULL; vp = next) {
		next = vp->next;
		to_list[to_count++] = vp;
		vp->next = NULL;
	}
	tailto = to_count;
	edited = rad_malloc(sizeof(*edited) * to_count);
	memset(edited, 0, sizeof(*edited) * to_count);

	DEBUG4("::: FROM %d TO %d MAX %d", from_count, to_count, count);

	/*
	 *	Now that we have the lists initialized, start working
	 *	over them.
	 */
	for (i = 0; i < from_count; i++) {
		int found;

		DEBUG4("::: Examining %s", from_list[i]->name);

		/*
		 *	Attribute should be appended, OR the "to" list
		 *	is empty, and we're supposed to replace or
		 *	"add if not existing".
		 */
		if (from_list[i]->operator == T_OP_ADD) goto append;

		found = FALSE;
		for (j = 0; j < to_count; j++) {
			if (edited[j]) continue;

			/*
			 *	Attributes aren't the same, skip them.
			 */
			if (from_list[i]->attribute != to_list[j]->attribute) {
				continue;
			}

			/*
			 *	We don't use a "switch" statement here
			 *	because we want to break out of the
			 *	"for" loop over 'j' in most cases.
			 */

			/*
			 *	Over-write the FIRST instance of the
			 *	matching attribute name.  We free the
			 *	one in the "to" list, and move over
			 *	the one in the "from" list.
			 */
			if (from_list[i]->operator == T_OP_SET) {
				DEBUG4("::: OVERWRITING %s FROM %d TO %d",
				       to_list[j]->name, i, j);
				pairfree(&to_list[j]);
				to_list[j] = from_list[i];
				from_list[i] = NULL;
				edited[j] = TRUE;
				break;
			}

			/*
			 *	Add the attribute only if it does not
			 *	exist... but it exists, so we stop
			 *	looking.
			 */
			if (from_list[i]->operator == T_OP_EQ) {
				found = TRUE;
				break;
			}

			/*
			 *	Delete all matching attributes from
			 *	"to"
			 */
			if ((from_list[i]->operator == T_OP_SUB) ||
			    (from_list[i]->operator == T_OP_CMP_EQ) ||
			    (from_list[i]->operator == T_OP_LE) ||
			    (from_list[i]->operator == T_OP_GE)) {
				int rcode;
				int old_op = from_list[i]->operator;

				/*
				 *	Check for equality.
				 */
				from_list[i]->operator = T_OP_CMP_EQ;

				/*
				 *	If equal, delete the one in
				 *	the "to" list.
				 */
				rcode = radius_compare_vps(NULL, from_list[i],
							   to_list[j]);
				/*
				 *	We may want to do more
				 *	subtractions, so we re-set the
				 *	operator back to it's original
				 *	value.
				 */
				from_list[i]->operator = old_op;

				switch (old_op) {
				case T_OP_CMP_EQ:
					if (rcode != 0) goto delete;
					break;

				case T_OP_SUB:
					if (rcode == 0) {
					delete:
						DEBUG4("::: DELETING %s FROM %d TO %d",
						       from_list[i]->name, i, j);
						pairfree(&to_list[j]);
						to_list[j] = NULL;
					}
					break;

					/*
					 *	Enforce <=.  If it's
					 *	>, replace it.
					 */
				case T_OP_LE:
					if (rcode > 0) {
						DEBUG4("::: REPLACING %s FROM %d TO %d",
						       from_list[i]->name, i, j);
						pairfree(&to_list[j]);
						to_list[j] = from_list[i];
						from_list[i] = NULL;
						edited[j] = TRUE;
					}
					break;

				case T_OP_GE:
					if (rcode < 0) {
						DEBUG4("::: REPLACING %s FROM %d TO %d",
						       from_list[i]->name, i, j);
						pairfree(&to_list[j]);
						to_list[j] = from_list[i];
						from_list[i] = NULL;
						edited[j] = TRUE;
					}
					break;
				}

				continue;
			}

			rad_assert(0 == 1); /* panic! */
		}

		/*
		 *	We were asked to add it if it didn't exist,
		 *	and it doesn't exist.  Move it over to the
		 *	tail of the "to" list, UNLESS it was already
		 *	moved by another operator.
		 */
		if (!found && from_list[i]) {
			if ((from_list[i]->operator == T_OP_EQ) ||
			    (from_list[i]->operator == T_OP_LE) ||
			    (from_list[i]->operator == T_OP_GE) ||
			    (from_list[i]->operator == T_OP_SET)) {
			append:
				DEBUG4("::: APPENDING %s FROM %d TO %d",
				       from_list[i]->name, i, tailto);
				to_list[tailto++] = from_list[i];
				from_list[i] = NULL;
			}
		}
	}

	/*
	 *	Delete attributes in the "from" list.
	 */
	for (i = 0; i < from_count; i++) {
		if (!from_list[i]) continue;

		pairfree(&from_list[i]);
	}
	free(from_list);

	DEBUG4("::: TO in %d out %d", to_count, tailto);

	/*
	 *	Re-chain the "to" list.
	 */
	*to = NULL;
	last = to;
	for (i = 0; i < tailto; i++) {
		if (!to_list[i]) continue;
		
		DEBUG4("::: to[%d] = %s", i, to_list[i]->name);

		/*
		 *	Mash the operator to a simple '='.  The
		 *	operators in the "to" list aren't used for
		 *	anything.  BUT they're used in the "detail"
		 *	file and debug output, where we don't want to
		 *	see the operators.
		 */
		to_list[i]->operator = T_OP_EQ;

		*last = to_list[i];
		last = &(*last)->next;

		/*
		 *	Fix dumb cache issues
		 */
		if ((i >= to_count) || edited[i]) {
			if (to_list[i]->attribute == PW_USER_NAME) {
				request->username = to_list[i];
				
			} else if (to_list[i]->attribute == PW_USER_PASSWORD) {
				request->password = to_list[i];
			}
		}
	}
	free(to_list);
	free(edited);
}


/*
 *     Copied shamelessly from conffile.c, to simplify the API for
 *     now...
 */
typedef enum conf_type {
	CONF_ITEM_INVALID = 0,
	CONF_ITEM_PAIR,
	CONF_ITEM_SECTION,
	CONF_ITEM_DATA
} CONF_ITEM_TYPE;

struct conf_item {
	struct conf_item *next;
	struct conf_part *parent;
	int lineno;
	const char *filename;
	CONF_ITEM_TYPE type;
};
struct conf_pair {
	CONF_ITEM item;
	char *attr;
	char *value;
	FR_TOKEN operator;
	FR_TOKEN value_type;
};

/*
 *	Add attributes to a list.
 */
int radius_update_attrlist(REQUEST *request, CONF_SECTION *cs,
			   VALUE_PAIR *input_vps, const char *name)
{
	CONF_ITEM *ci;
	VALUE_PAIR *newlist, *vp;
	VALUE_PAIR **output_vps = NULL;
	REQUEST *request_vps = request;

	if (!request || !cs) return RLM_MODULE_INVALID;

	/*
	 *	If we are an inner tunnel session, permit the
	 *	policy to update the outer lists directly.
	 */
	if (memcmp(name, "outer.", 6) == 0) {
		if (!request->parent) return RLM_MODULE_NOOP;

		request_vps = request->parent;
		name += 6;
	}

	if (strcmp(name, "request") == 0) {
		output_vps = &request_vps->packet->vps;

	} else if (strcmp(name, "reply") == 0) {
		output_vps = &request_vps->reply->vps;

	} else if (strcmp(name, "proxy-request") == 0) {
		if (request->proxy) output_vps = &request_vps->proxy->vps;

	} else if (strcmp(name, "proxy-reply") == 0) {
		if (request->proxy_reply) output_vps = &request->proxy_reply->vps;

	} else if (strcmp(name, "config") == 0) {
		output_vps = &request_vps->config_items;

	} else if (strcmp(name, "control") == 0) {
		output_vps = &request_vps->config_items;

	} else {
		return RLM_MODULE_INVALID;
	}

	if (!output_vps) return RLM_MODULE_NOOP; /* didn't update the list */

	newlist = paircopy(input_vps);
	if (!newlist) {
		DEBUG2("Out of memory");
		return RLM_MODULE_FAIL;
	}

	vp = newlist;
	for (ci=cf_item_find_next(cs, NULL);
	     ci != NULL;
	     ci=cf_item_find_next(cs, ci)) {
		CONF_PAIR *cp;

		/*
		 *	This should never happen.
		 */
		if (cf_item_is_section(ci)) {
			pairfree(&newlist);
			return RLM_MODULE_INVALID;
		}

		cp = cf_itemtopair(ci);

		/*
		 *	The VP && CF lists should be in sync.  If they're
		 *	not, panic.
		 */
		if (vp->flags.do_xlat) {
			const char *value;
			char buffer[2048];

			value = expand_string(buffer, sizeof(buffer), request,
					      cp->value_type, cp->value);
			if (!value) {
				pairfree(&newlist);
				return RLM_MODULE_INVALID;
			}

			if (!pairparsevalue(vp, value)) {
				DEBUG2("ERROR: Failed parsing value \"%s\" for attribute %s: %s",
				       value, vp->name, librad_errstr);
				pairfree(&newlist);
				return RLM_MODULE_FAIL;
			}
			vp->flags.do_xlat = 0;
		}
		vp = vp->next;
	}

	my_pairmove(request, output_vps, newlist);

	return RLM_MODULE_UPDATED;
}
