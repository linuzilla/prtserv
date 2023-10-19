/*
 *	autofree.h
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#ifndef __AUTOFREE_H__
#define __AUTOFREE_H__

struct autofree_interface_t {
	void	(*free)(void *ptr);
	void	(*free_all)(void);
	void	(*dispose)(void);
};

struct autofree_interface_t *	init_autofree_interface (const int len);

#endif
