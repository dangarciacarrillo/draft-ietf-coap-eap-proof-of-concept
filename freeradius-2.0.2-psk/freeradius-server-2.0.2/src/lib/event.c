/*
 * event.c	Non-thread-safe event handling, specific to a RADIUS
 *		server.
 *
 * Version:	$Id: event.c,v 1.18 2008/02/11 15:09:56 aland Exp $
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 *  Copyright 2007  The FreeRADIUS server project
 *  Copyright 2007  Alan DeKok <aland@ox.org>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: event.c,v 1.18 2008/02/11 15:09:56 aland Exp $")

#include <freeradius-devel/libradius.h>
#include <freeradius-devel/event.h>

typedef struct fr_event_fd_t {
	int			fd;
	fr_event_fd_handler_t	handler;
	void			*ctx;
} fr_event_fd_t;

#define FR_EV_MAX_FDS (256)


struct fr_event_list_t {
	rbtree_t	*times;

	int		changed;
	int		maxfd;

	int		exit;

	fr_event_status_t status;

	struct timeval  now;
	int		dispatch;

	int		max_readers;
	fd_set		read_fds;
	fr_event_fd_t	readers[FR_EV_MAX_FDS];
};

/*
 *	Internal structure for managing events.
 */
struct fr_event_t {
	fr_event_callback_t	callback;
	void			*ctx;
	struct timeval		when;
	fr_event_t		**ev_p;
	rbnode_t		*node;
};


static int fr_event_list_time_cmp(const void *one, const void *two)
{
	const fr_event_t *a = one;
	const fr_event_t *b = two;

	if (a->when.tv_sec < b->when.tv_sec) return -1;
	if (a->when.tv_sec > b->when.tv_sec) return +1;

	if (a->when.tv_usec < b->when.tv_usec) return -1;
	if (a->when.tv_usec > b->when.tv_usec) return +1;

	return 0;
}


void fr_event_list_free(fr_event_list_t *el)
{
	if (!el) return;

	rbtree_free(el->times);
	free(el);
}


fr_event_list_t *fr_event_list_create(fr_event_status_t status)
{
	int i;
	fr_event_list_t *el;

	el = malloc(sizeof(*el));
	if (!el) return NULL;
	memset(el, 0, sizeof(*el));

	el->times = rbtree_create(fr_event_list_time_cmp,
				  free, 0);
	if (!el->times) {
		fr_event_list_free(el);
		return NULL;
	}

	for (i = 0; i < FR_EV_MAX_FDS; i++) {
		el->readers[i].fd = -1;
	}

	el->status = status;
	el->changed = 1;	/* force re-set of fds's */

	return el;
}

int fr_event_list_num_elements(fr_event_list_t *el)
{
	if (!el) return 0;

	return rbtree_num_elements(el->times);
}


int fr_event_delete(fr_event_list_t *el, fr_event_t **ev_p)
{
	fr_event_t *ev;

	if (!el || !ev_p || !*ev_p) return 0;

	ev = *ev_p;
	if (ev->ev_p) *(ev->ev_p) = NULL;
	*ev_p = NULL;

	rbtree_delete(el->times, ev->node);

	return 1;
}


int fr_event_insert(fr_event_list_t *el,
		      fr_event_callback_t callback,
		      void *ctx, struct timeval *when,
		      fr_event_t **ev_p)
{
	fr_event_t *ev;

	if (!el || !callback | !when) return 0;

	if (ev_p && *ev_p) fr_event_delete(el, ev_p);

	ev = malloc(sizeof(*ev));
	if (!ev) return 0;
	memset(ev, 0, sizeof(*ev));

	ev->callback = callback;
	ev->ctx = ctx;
	ev->when = *when;
	ev->ev_p = ev_p;

	/*
	 *	There's a tiny chance that two events will be
	 *	scheduled at the same time.  If this happens, we
	 *	increase the usec counter by 1, in order to avoid the
	 *	duplicate.  If we can't insert it after 10 tries, die.
	 */
	ev->node = rbtree_insertnode(el->times, ev);
	if (!ev->node) {
		if (rbtree_finddata(el->times, ev)) {
			int i;

			for (i = 0; i < 10; i++) {
				ev->when.tv_usec++;
				if (ev->when.tv_usec >= 1000000) {
					ev->when.tv_usec = 0;
					ev->when.tv_sec++;
				}

				if (rbtree_finddata(el->times, ev)) {
					continue;
				}

				ev->node = rbtree_insertnode(el->times, ev);
				if (!ev->node) { /* error */
					break;
				}

				if (ev_p) *ev_p = ev;
				return 1;
			}

		}
		free(ev);
		return 0;
	}

	if (ev_p) *ev_p = ev;
	return 1;
}


int fr_event_run(fr_event_list_t *el, struct timeval *when)
{
	fr_event_callback_t callback;
	void *ctx;
	fr_event_t *ev;

	if (!el) return 0;

	if (rbtree_num_elements(el->times) == 0) {
		when->tv_sec = 0;
		when->tv_usec = 0;
		return 0;
	}

	ev = rbtree_min(el->times);
	if (!ev) {
		when->tv_sec = 0;
		when->tv_usec = 0;
		return 0;
	}

	/*
	 *	See if it's time to do this one.
	 */
	if ((ev->when.tv_sec > when->tv_sec) ||
	    ((ev->when.tv_sec == when->tv_sec) &&
	     (ev->when.tv_usec > when->tv_usec))) {
		*when = ev->when;
		return 0;
	}

	callback = ev->callback;
	ctx = ev->ctx;

	/*
	 *	Delete the event before calling it.
	 */
	fr_event_delete(el, &ev);

	callback(ctx);
	return 1;
}


int fr_event_now(fr_event_list_t *el, struct timeval *when)
{
	if (!el || !when || !el->dispatch) return 0;

	*when = el->now;
	return 1;
}


int fr_event_fd_insert(fr_event_list_t *el, int type, int fd,
		       fr_event_fd_handler_t handler, void *ctx)
{
	int i;
	fr_event_fd_t *ef;

	if (!el || (fd < 0) || !handler || !ctx) return 0;

	if (type != 0) return 0;

	if (el->max_readers >= FR_EV_MAX_FDS) return 0;

	ef = NULL;
	for (i = 0; i <= el->max_readers; i++) {
		/*
		 *	Be fail-safe on multiple inserts.
		 */
		if (el->readers[i].fd == fd) {
			if ((el->readers[i].handler != handler) ||
			    (el->readers[i].ctx != ctx)) {
				return 0;
			}

			/*
			 *	No change.
			 */
			return 1;
		}

		if (el->readers[i].fd < 0) {
			ef = &el->readers[i];

			if (i == el->max_readers) el->max_readers = i + 1;
			break;
		}
	}

	if (!ef) return 0;

	ef->fd = fd;
	ef->handler = handler;
	ef->ctx = ctx;

       	if (fd > el->maxfd) el->maxfd = fd;
	el->changed = 1;

	return 1;
}

int fr_event_fd_delete(fr_event_list_t *el, int type, int fd)
{
	int i;

	if (!el || (fd < 0)) return 0;

	if (type != 0) return 0;

	for (i = 0; i < el->max_readers; i++) {
		if (el->readers[i].fd == fd) {
			el->readers[i].fd = -1;
			if ((i + 1) == el->max_readers) el->max_readers = i;
			if (fd == el->maxfd) el->maxfd--;
			el->changed = 1;
			return 1;
		}
	}

	return 0;
}			 


void fr_event_loop_exit(fr_event_list_t *el, int code)
{
	if (!el) return;

	el->exit = code;
}


int fr_event_loop(fr_event_list_t *el)
{
	int i, rcode;
	struct timeval when, *wake;
	fd_set read_fds;

	/*
	 *	Cache the list of FD's to watch.
	 */
	if (el->changed) {
		FD_ZERO(&el->read_fds);

		for (i = 0; i < el->max_readers; i++) {
			if (el->readers[i].fd < 0) continue;
			FD_SET(el->readers[i].fd, &el->read_fds);
		}

		el->changed = 0;
	}

	el->exit = 0;
	el->dispatch = 1;

	while (!el->exit) {

		/*
		 *	Find the first event.  If there's none, we wait
		 *	on the socket forever.
		 */
		when.tv_sec = 0;
		when.tv_usec = 0;

		if (rbtree_num_elements(el->times) > 0) {
			fr_event_t *ev;

			ev = rbtree_min(el->times);
			if (!ev) _exit(42);

			gettimeofday(&el->now, NULL);

			if (timercmp(&el->now, &ev->when, <)) {
				when = ev->when;
				when.tv_sec -= el->now.tv_sec;
				when.tv_usec -= el->now.tv_usec;
				if (when.tv_usec < 0) {
					when.tv_sec--;
					when.tv_usec += 1000000;
				}
			} else { /* we've passed the event time */
				when.tv_sec = 0;
				when.tv_usec = 0;
			}

			wake = &when;
		} else {
			wake = NULL;
		}

		/*
		 *	Tell someone what the status is.
		 */
		if (el->status) el->status(wake);

		read_fds = el->read_fds;
		rcode = select(el->maxfd + 1, &read_fds, NULL, NULL, wake);
		if ((rcode < 0) && (errno != EINTR)) {
			el->dispatch = 0;
			return 0;
		}

		if (rbtree_num_elements(el->times) > 0) {
			do {
				gettimeofday(&el->now, NULL);
				when = el->now;
			} while (fr_event_run(el, &when) == 1);
		}
		
		if (rcode <= 0) continue;

		el->changed = 0;

		for (i = 0; i < el->max_readers; i++) {
			fr_event_fd_t *ef = &el->readers[i];

			if (ef->fd < 0) continue;

			if (!FD_ISSET(ef->fd, &read_fds)) continue;
			
			ef->handler(el, ef->fd, ef->ctx);

			if (el->changed) break;
		}
	}

	el->dispatch = 0;
	return el->exit;
}


#ifdef TESTING

/*
 *  cc -g -I .. -c rbtree.c -o rbtree.o && cc -g -I .. -c isaac.c -o isaac.o && cc -DTESTING -I .. -c event.c  -o event_mine.o && cc event_mine.o rbtree.o isaac.o -o event
 *
 *  ./event
 *
 *  And hit CTRL-S to stop the output, CTRL-Q to continue.
 *  It normally alternates printing the time and sleeping,
 *  but when you hit CTRL-S/CTRL-Q, you should see a number
 *  of events run right after each other.
 *
 *  OR
 *
 *   valgrind --tool=memcheck --leak-check=full --show-reachable=yes ./event
 */

static void print_time(void *ctx)
{
	struct timeval *when = ctx;

	printf("%d.%06d\n", when->tv_sec, when->tv_usec);
	fflush(stdout);
}

static fr_randctx rand_pool;

static uint32_t event_rand(void)
{
	uint32_t num;

	num = rand_pool.randrsl[rand_pool.randcnt++];
	if (rand_pool.randcnt == 256) {
		fr_isaac(&rand_pool);
		rand_pool.randcnt = 0;
	}

	return num;
}


#define MAX 100
int main(int argc, char **argv)
{
	int i, rcode;
	struct timeval array[MAX];
	struct timeval now, when;
	fr_event_list_t *el;

	el = fr_event_list_create();
	if (!el) exit(1);

	memset(&rand_pool, 0, sizeof(rand_pool));
	rand_pool.randrsl[1] = time(NULL);

	fr_randinit(&rand_pool, 1);
	rand_pool.randcnt = 0;

	gettimeofday(&array[0], NULL);
	for (i = 1; i < MAX; i++) {
		array[i] = array[i - 1];

		array[i].tv_usec += event_rand() & 0xffff;
		if (array[i].tv_usec > 1000000) {
			array[i].tv_usec -= 1000000;
			array[i].tv_sec++;
		}
		fr_event_insert(el, print_time, &array[i], &array[i]);
	}

	while (fr_event_list_num_elements(el)) {
		gettimeofday(&now, NULL);
		when = now;
		if (!fr_event_run(el, &when)) {
			int delay = (when.tv_sec - now.tv_sec) * 1000000;
			delay += when.tv_usec;
			delay -= now.tv_usec;

			printf("\tsleep %d\n", delay);
			fflush(stdout);
			usleep(delay);
		}
	}

	fr_event_list_free(el);

	return 0;
}
#endif
