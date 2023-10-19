/*
 *	x_object.h
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#ifndef __X_OBJECT_H__
#define __X_OBJECT_H__

#define X_OBJECT_TYPE_EMPTY		0
#define X_OBJECT_TYPE_INTEGER		1
#define X_OBJECT_TYPE_REAL		2
#define X_OBJECT_TYPE_STRING		3
#define X_OBJECT_TYPE_STRING_LIST	4
#define X_OBJECT_TYPE_LIST		5
#define X_OBJECT_TYPE_HASH		6
#define X_OBJECT_TYPE_STRING_HASH	7

struct x_object_t;

struct x_object_interface_t {
	struct x_object_t * (*newobj)(void);

	int		(*type_of)(struct x_object_t *);
	int		(*is_array)(struct x_object_t *);
	int		(*is_empty)(struct x_object_t *);
	void *		(*dispose)(struct x_object_t *);

	double		(*realnum)(struct x_object_t *);
	int		(*intval)(struct x_object_t *);
	char *		(*string)(struct x_object_t *);
	int		(*count)(struct x_object_t *);

	void		(*empty)(struct x_object_t *);

	void		(*setint)(struct x_object_t *self, const int value);
	void		(*setreal)(struct x_object_t *self, const double value);
	void		(*setstr)(struct x_object_t *self, const char *str);

	char **		(*str_array)(struct x_object_t *);

	int		(*copy)(struct x_object_t *self,
						struct x_object_t *xobj);

	char *		(*get)(struct x_object_t *self, const char *keystr);
	int		(*put)(struct x_object_t *self,
					char *keystr, char *value);
	int		(*del)(struct x_object_t *self, const char *keystr);
	int		(*is_exist)(struct x_object_t *self, const char *key);
	int		(*get_first)(struct x_object_t *self,
						char **key, char **value);
	int		(*get_next)(struct x_object_t *self,
						char **key, char **value);

	int		(*str_push)(struct x_object_t *self,
						const char *str);
	char *		(*str_pop)(struct x_object_t *self);

	int		(*Push)(struct x_object_t *self,
						struct x_object_t *xobj);
	int		(*Unshift)(struct x_object_t *self,
						struct x_object_t *xobj);
	int		(*push)(struct x_object_t *self,
						struct x_object_t *xobj);
	int		(*unshift)(struct x_object_t *self,
						struct x_object_t *xobj);
	struct x_object_t * (*pop)(struct x_object_t *self);
	struct x_object_t * (*shift)(struct x_object_t *self);
	struct x_object_t * (*clone)(struct x_object_t *self);
	void *		(*cleanup)(void);
};

struct x_object_interface_t *	init_x_object_interface (void);

#endif
