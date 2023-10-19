/*
 *	xtimer.h
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#ifndef __XTIMER_H__
#define __XTIMER_H__

#include <sys/time.h>

struct xtimer_pd_t {
	struct timeval	tstart;
	struct timeval	tnow;
	struct timeval	tdiff;
};

struct xtimer_t {
	struct xtimer_pd_t	pd;

	void	(*dispose)(struct xtimer_t *);
	void	(*start)(struct xtimer_t *);
	int	(*elapsed)(struct xtimer_t *);
	void	(*print)(struct xtimer_t *);
};

struct xtimer_t * new_xtimer (void);

#endif
