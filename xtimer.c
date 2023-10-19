/*
 *	xtimer.c
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#include <stdio.h>
#include <stdlib.h>
#include "xtimer.h"

static void tvsub (struct timeval *tdiff,
			const struct timeval *t1, const struct timeval *t0) {
	tdiff->tv_sec  = t1->tv_sec  - t0->tv_sec;
	tdiff->tv_usec = t1->tv_usec - t0->tv_usec;

	if (tdiff->tv_usec < 0) tdiff->tv_sec--, tdiff->tv_usec += 1000000;
}

static void xt_dispose (struct xtimer_t *self) {
	free (self);
}

static void xt_start (struct xtimer_t *self) {
	gettimeofday (&self->pd.tstart, NULL);
}

static int xt_elapsed (struct xtimer_t *self) {
	struct timeval	*t = &self->pd.tdiff;

	gettimeofday (&self->pd.tnow, NULL);

	tvsub (t, &self->pd.tnow, &self->pd.tstart);
	return (t->tv_sec * 1000) + (t->tv_usec / 1000);
}

static void xt_print (struct xtimer_t *self) {
	struct timeval	*t = &self->pd.tdiff;

	fprintf (stderr, "[%02ld:%02ld:%02ld.%06ld]\n",
			t->tv_sec / 3600,
			t->tv_sec % 3600 / 60,
			t->tv_sec % 60,
			t->tv_usec);
}

struct xtimer_t * new_xtimer (void) {
	struct xtimer_t		*self;

	if ((self = malloc (sizeof (struct xtimer_t))) != NULL) {
		self->dispose	= xt_dispose;
		self->start	= xt_start;
		self->elapsed	= xt_elapsed;
		self->print	= xt_print;

		self->start (self);
	}

	return self;
}
