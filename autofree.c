/*
 *	autofree.c
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#include <stdio.h>
#include <stdlib.h>
#include "autofree.h"

static struct autofree_interface_t	afobjintf;
static struct autofree_interface_t	*intf = NULL;

struct membuf_t { void	*ptr; };

static struct membuf_t		*mbuf = NULL;
static int			mbuf_len;
static int			mbuf_idx;
static int			reference_count = 0;


static void af_free (void *ptr) {
	int	i;

	if (mbuf == NULL) {
		if (ptr != NULL) free (ptr);
		return;
	} else if (ptr == NULL) {
		return;
	}

	i = mbuf_idx;
	mbuf_idx = (mbuf_idx + 1) % mbuf_len;

	if (mbuf[i].ptr != NULL) free (mbuf[i].ptr);
	mbuf[i].ptr = NULL;
}

static void af_free_all (void) {
	int	i;

	if (intf == NULL) return;

	for (i = 0; i < mbuf_len; i++) {
		if (mbuf[i].ptr == NULL) free (mbuf[i].ptr);
		mbuf[i].ptr = NULL;
	}
}

static void af_dispose (void) {
	if (intf == NULL) return;
	if (--reference_count > 0) return;

	af_free_all ();

	if (mbuf != NULL) {
		free (mbuf);
		mbuf = NULL;
		// printf ("dispose\n");
	}
}

struct autofree_interface_t * init_autofree_interface (const int buflen) {
	void	*ptr;
	int	i, len;

	len = (buflen <= 10) ? 10 : buflen;

	if (intf == NULL) {
		mbuf_len = 0;
		mbuf_idx = 0;

		if ((mbuf = malloc (len * sizeof (struct membuf_t))) != NULL) {
			mbuf_len = len;
			for (i = 0; i < mbuf_len; i++) mbuf[i].ptr = NULL;
		}

		intf = &afobjintf;

		intf->free	= af_free;
		intf->free_all	= af_free_all;
		intf->dispose	= af_dispose;
	} else if (len > mbuf_len) {

		if ((ptr = realloc (mbuf, len)) != NULL) {
			mbuf = ptr;

			for (i = mbuf_len; i < len; i++) mbuf[i].ptr = NULL;

			mbuf_len = len;
		}
	}

	reference_count++;

	return intf;
}
