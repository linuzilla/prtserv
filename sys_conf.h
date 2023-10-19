/*
 *	sys_conf.h
 *
 *	Copyright (c) 2002, Jiann-Ching Liu
 */

#ifndef __SYSTEM_CONFIGURATION_H_
#define __SYSTEM_CONFIGURATION_H_

#include <sys/types.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct sysconf_t {
	char*	(*getstr)(const char *key);
	int	(*getint)(const char *key);
	int*	(*intlist)(const char *key, int *len);
	char**	(*strlist)(const char *key, int *len);
	char*	(*first_key)(void);
	char*	(*next_key)(void);
	void*	(*addint)(const char *, const char *);
	void*	(*addint_x)(const char *, const int); 
	void*	(*addstr)(const char *, const char *);
	void*	(*addflag_on)(const char *);
	void*	(*addflag_off)(const char *);
	void*	(*add_int_list)(const char *, int *, int);
	void*	(*add_str_list)(const char *, char **, int);
	int	(*add_special)(const char, const int, const int);
	int	(*get_special)(const char, int *);
};

struct sysconf_t * initial_sysconf_module (char *file, 
					const char *variable, const int value);

#if defined(__cplusplus)
}
#endif

#endif
