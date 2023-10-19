/*
 *	x_object.c
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "x_object.h"
#include "autofree.h"

static int				object_count = 0;
static int				error_level = 3;
static struct x_object_interface_t	objintf;
static struct x_object_interface_t	*objptr = NULL;
static char				*nullstr = "";
static struct autofree_interface_t	*autofree_intf = NULL;



struct x_object_list_t {
	char			*key;
	struct x_object_t	*xobj;
	struct x_object_list_t	*next;
	struct x_object_list_t	*prev;
};

struct x_object_t {
	short	obj_type;
	union {
		int			intval;
		double			realnumber;
		char			*string;
		struct x_object_list_t	*list;
	} val;
	struct x_object_list_t		*tail;
	struct x_object_list_t		*read_ptr;
	int				list_count;
	char				**strlist;
	short				strlist_ok;
	int				strlist_cnt;
};

static struct x_object_list_t *internal_new_list_obj_entry (
					struct x_object_t *xobj,
					char *skey, const char *str,
					const int duplicate) {
	struct x_object_list_t	*list;

	if ((list = malloc (sizeof (struct x_object_list_t))) == NULL) {
		if (error_level >= 2) perror ("malloc");
		return NULL;
	} else {
		list->key = NULL;
	}

	if (duplicate) {
		if (xobj != NULL) {
			if ((list->xobj = objptr->clone (xobj)) == NULL) {
				free (list);
				return NULL;
			}
		} else {
			if (str == NULL) {
				list->xobj = NULL;
			} else {
				list->xobj = (void *) strdup (str);
			}

			if (skey != NULL) list->key = strdup (skey);
			// printf ("[:%s:]\n", (char *) list->key);
		}
	} else {
		if (xobj != NULL) {
			list->xobj = xobj;
		} else {
			list->xobj = (void *) str;

			if (skey != NULL) list->key = skey;
		}
	}

	list->next = list->prev = NULL;

	return list;
};

static void internal_free_list_obj_entry (struct x_object_list_t *xobj) {
	free (xobj);
}


static int xobj_internal_push (struct x_object_t *self,
					struct x_object_t *xobj,
					const char *str,
					const int duplicate) {
	struct x_object_list_t	*list;

	self->strlist_ok = 0;

	switch (self->obj_type) {
	case X_OBJECT_TYPE_EMPTY:
		break;
	case X_OBJECT_TYPE_LIST:
		if (xobj == NULL) return 0;
		break;
	case X_OBJECT_TYPE_STRING_LIST:
		if (xobj != NULL) return 0;
		// fprintf (stderr, "push %s\n", str);
		break;
	default:
		return 0;
	}

	if ((list = internal_new_list_obj_entry (
				xobj, NULL, str, duplicate)) == NULL) return 0;



	if (self->obj_type == X_OBJECT_TYPE_EMPTY) {
		if (xobj == NULL) {
			self->obj_type   = X_OBJECT_TYPE_STRING_LIST;
		} else {
			self->obj_type   = X_OBJECT_TYPE_LIST;
		}
		self->val.list   = list;
		self->tail       = list;
		self->list_count = 1;
	} else if (self->obj_type == X_OBJECT_TYPE_LIST) {
		self->tail->next = list;
		list->prev       = self->tail;
		self->tail       = list;

		++self->list_count;
	} else if (self->obj_type == X_OBJECT_TYPE_STRING_LIST) {
		self->tail->next = list;
		list->prev       = self->tail;
		self->tail       = list;
		self->read_ptr	= NULL;

		++self->list_count;
	}

	return 1;
}

static int xobj_push (struct x_object_t *self, struct x_object_t *xobj) {
	return xobj_internal_push (self, xobj, NULL, 1);
}

static int xobj_Push (struct x_object_t *self, struct x_object_t *xobj) {
	return xobj_internal_push (self, xobj, NULL, 0);
}

static int xobj_str_push (struct x_object_t *self, const char *str) {
	return xobj_internal_push (self, NULL, str, 1);
}

static char * xobj_str_pop (struct x_object_t *self) {
	struct x_object_list_t	*list;
	char			*str = NULL;

	self->strlist_ok = 0;

	if (self->obj_type != X_OBJECT_TYPE_STRING_LIST) return NULL;

	if (self->list_count == 0) {
		self->obj_type = X_OBJECT_TYPE_EMPTY;
		return NULL;
	} else if (self->list_count == 1) {
		self->list_count = 0;
		list = self->val.list;
		self->val.list = NULL;
		str = (char *) list->xobj;
		internal_free_list_obj_entry (list);
	} else {
		struct x_object_list_t	*ptr;

		--self->list_count;
		list = self->tail;
		ptr = list->prev;
		ptr->next = NULL;
		self->tail = ptr;

		str = (char *) list->xobj;
		internal_free_list_obj_entry (list);
	}

	autofree_intf->free (str);
	return str;
}

static struct x_object_t * xobj_pop (struct x_object_t *pd) {
	struct x_object_list_t	*list;
	struct x_object_t	*xobj = NULL;

	pd->strlist_ok = 0;

	if (pd->obj_type != X_OBJECT_TYPE_LIST) return NULL;

	if (pd->list_count == 0) {
		pd->obj_type = X_OBJECT_TYPE_EMPTY;
		return NULL;
	} else if (pd->list_count == 1) {
		pd->list_count = 0;
		list = pd->val.list;
		pd->val.list = NULL;
		xobj = list->xobj;
		internal_free_list_obj_entry (list);
	} else {
		struct x_object_list_t	*ptr;

		--pd->list_count;
		list = pd->tail;
		ptr = list->prev;
		ptr->next = NULL;
		pd->tail = ptr;
		xobj = list->xobj;
		internal_free_list_obj_entry (list);
	}

	return xobj;
}

static int xobj_internal_unshift (struct x_object_t *pd,
					struct x_object_t *xobj,
					const int duplicate) {
	struct x_object_list_t	*list;

	pd->strlist_ok = 0;

	if ((pd->obj_type != X_OBJECT_TYPE_EMPTY) && 
				(pd->obj_type != X_OBJECT_TYPE_LIST)) {
		return 0;
	}

	if ((list = internal_new_list_obj_entry (
				xobj, NULL, NULL, duplicate)) == NULL) return 0;


	if (pd->obj_type == X_OBJECT_TYPE_EMPTY) {
		pd->obj_type = X_OBJECT_TYPE_LIST;
		pd->val.list = list;
		pd->list_count = 1;
		pd->tail     = list;
	} else if (pd->obj_type == X_OBJECT_TYPE_LIST) {
		pd->val.list->prev = list;
		list->next         = pd->val.list;
		pd->val.list       = list;

		++pd->list_count;
	}

	return 0;
}

static int xobj_unshift (struct x_object_t *self, struct x_object_t *xobj) {
	return xobj_internal_unshift (self, xobj, 1);
}

static int xobj_Unshift (struct x_object_t *self, struct x_object_t *xobj) {
	return xobj_internal_unshift (self, xobj, 0);
}

static struct x_object_t * xobj_shift (struct x_object_t *pd) {
	struct x_object_list_t	*list;
	struct x_object_t	*xobj = NULL;

	pd->strlist_ok = 0;

	if (pd->obj_type != X_OBJECT_TYPE_LIST) return NULL;

	if (pd->list_count == 0) {
		pd->obj_type = X_OBJECT_TYPE_EMPTY;
		return NULL;
	} else if (pd->list_count == 1) {
		pd->list_count = 0;
		list = pd->val.list;
		pd->val.list = NULL;
		xobj = list->xobj;
		internal_free_list_obj_entry (list);
	} else {
		struct x_object_list_t	*ptr;

		--pd->list_count;
		list = pd->tail;
		ptr = list->prev;
		ptr->next = NULL;
		xobj = list->xobj;
		internal_free_list_obj_entry (list);
	}

	return xobj;
}


static int xobj_copy (struct x_object_t *self, struct x_object_t *srcobj) {
	struct x_object_list_t	*list, *ptr;

	/* [FIXME]: Do not support list yet!! */

	if (srcobj->obj_type == X_OBJECT_TYPE_LIST) return 0;

	objptr->empty (self);

	memcpy (self, srcobj, sizeof (struct x_object_t));

	self->strlist = NULL;
	self->strlist_cnt = 0;

	switch (self->obj_type) {
	case X_OBJECT_TYPE_STRING:
		if ((self->val.string =
				strdup (srcobj->val.string)) == NULL) {
			objptr->empty (self);
			return 0;
		} else {
			self->list_count = strlen (self->val.string);
		}

		// fprintf (stderr, "<:%s:>\n", xobj->pd->val.string);
		break;
	case X_OBJECT_TYPE_STRING_LIST:
		/* [FIXME] */
		break;
	case X_OBJECT_TYPE_LIST:
		if ((list = internal_new_list_obj_entry (self,
						NULL, NULL, 1)) == NULL) {
			objptr->empty (self);
			return 0;
		} else {
			/* [FIXME] Not complete yet!  */
			ptr = srcobj->val.list;
		}
		break;
	case X_OBJECT_TYPE_HASH:
		/* [FIXME] */
		break;
	case X_OBJECT_TYPE_STRING_HASH:
		/* [FIXME] */
		break;
	}

	return 1;
}

static struct x_object_t * xobj_clone (struct x_object_t *self) {
	struct x_object_t	*xobj;

	if ((xobj = objptr->newobj ()) != NULL) objptr->copy (xobj, self);

	return xobj;
}

static void * xobj_dispose (struct x_object_t *self) {
	objptr->empty (self);

	free (self);
	object_count--;

	return NULL;
}

static int xobj_type_of (struct x_object_t *self) {
	return self->obj_type;
}

static int xobj_is_array (struct x_object_t *self) {
	return self->obj_type == X_OBJECT_TYPE_LIST ? 1 : 0;
}

static int xobj_is_empty (struct x_object_t *self) {
	return self->obj_type == X_OBJECT_TYPE_EMPTY ? 1 : 0;
}

static int xobj_intval (struct x_object_t *self) {

	switch (self->obj_type) {
	case X_OBJECT_TYPE_INTEGER:
		return self->val.intval;
	case X_OBJECT_TYPE_LIST:
	case X_OBJECT_TYPE_STRING_LIST:
	case X_OBJECT_TYPE_HASH:
	case X_OBJECT_TYPE_STRING_HASH:
		return self->list_count;
	default:
		return 0;
	}
}

static int xobj_count (struct x_object_t *self) {

	switch (self->obj_type) {
	case X_OBJECT_TYPE_LIST:
	case X_OBJECT_TYPE_STRING_LIST:
	case X_OBJECT_TYPE_HASH:
	case X_OBJECT_TYPE_STRING_HASH:
		return self->list_count;
	default:
		return 0;
	}

	return 0;
}

static double xobj_realnum (struct x_object_t *self) {
	if (self->obj_type == X_OBJECT_TYPE_REAL) return self->val.realnumber;
	return 0.0;
}

static char * xobj_string (struct x_object_t *self) {

	if (self->obj_type == X_OBJECT_TYPE_STRING) return self->val.string;
	return NULL;
}

static int internal_gen_strlist (struct x_object_t *self) {
	struct x_object_list_t	*list;
	int			i, len;

	if ((self->obj_type != X_OBJECT_TYPE_LIST) &&
			(self->obj_type != X_OBJECT_TYPE_STRING_LIST)) return 0;

	len = self->list_count;


	if (len > self->strlist_cnt) {
		free (self->strlist);
		self->strlist_cnt = self->strlist_ok = 0;
		self->strlist = NULL;
	}

	if (self->strlist == NULL) {
		if ((self->strlist = malloc (len * sizeof (char *))) == NULL) {
			self->strlist_cnt = self->strlist_ok = 0;
			return 0;
		}
	}

	if (self->strlist_ok) return 1;

	list = self->val.list;

	for (i = 0; i < len; i++) {
		if (list == NULL) {
			fprintf (stderr, "Internal error!!\n");
			self->strlist[i] = NULL;
			break;
		} else if (self->obj_type == X_OBJECT_TYPE_LIST) {
			self->strlist[i] = objptr->string (list->xobj);
			list = list->next;
		} else if (self->obj_type == X_OBJECT_TYPE_STRING_LIST) {
			self->strlist[i] = (void *) list->xobj;
			list = list->next;
		}
	}

	self->strlist_ok = 1;
	self->strlist_cnt = len;

	return 1;
}

static char ** xobj_string_array (struct x_object_t *self) {

	if ((self->obj_type != X_OBJECT_TYPE_LIST) &&
			(self->obj_type != X_OBJECT_TYPE_STRING_LIST)) return 0;


	if (internal_gen_strlist (self)) return self->strlist;

	return NULL;
}

static void xobj_empty (struct x_object_t *self) {
	struct x_object_list_t	*list;
	short			otype = self->obj_type;

	self->obj_type = X_OBJECT_TYPE_EMPTY;

	switch (otype) {
	case X_OBJECT_TYPE_EMPTY:
		break;
	case X_OBJECT_TYPE_STRING:
		if (self->val.string != NULL) free (self->val.string);
		break;
	case X_OBJECT_TYPE_INTEGER:
		break;
	case X_OBJECT_TYPE_REAL:
		break;
	case X_OBJECT_TYPE_LIST:
	case X_OBJECT_TYPE_STRING_LIST:
	case X_OBJECT_TYPE_HASH:
	case X_OBJECT_TYPE_STRING_HASH:
		/* FIXME */
		list = self->val.list;

		if (list != NULL) {
			struct x_object_list_t	*ptr;

			do {
				ptr = list->next;

				switch (otype) {
				case X_OBJECT_TYPE_LIST:
					objptr->dispose (list->xobj);
					break;
				case X_OBJECT_TYPE_STRING_LIST:
					if (list->xobj != NULL) {
						free (list->xobj);
					}
					break;
				case X_OBJECT_TYPE_HASH:
					free (list->key);
					objptr->dispose (list->xobj);
					break;
				case X_OBJECT_TYPE_STRING_HASH:
					free (list->key);
					if (list->xobj != NULL) {
						free (list->xobj);
					}
					break;
				}

				internal_free_list_obj_entry (list);
				list = ptr;
			} while (list != NULL);
		}

		self->strlist_ok = self->strlist_cnt = 0;
		if (self->strlist != NULL) free (self->strlist);
		self->strlist = NULL;
		break;
	}
}

static void xobj_setint (struct x_object_t *self, const int value) {
	objptr->empty (self);
	self->obj_type = X_OBJECT_TYPE_INTEGER;
	self->val.intval = value;
}

static void xobj_setreal (struct x_object_t *self, const double value) {
	objptr->empty (self);
	self->obj_type = X_OBJECT_TYPE_REAL;
	self->val.realnumber = value;
}

static void xobj_setstr (struct x_object_t *self, const char *value) {
	int			len;

	if (value == NULL) {
		objptr->empty (self);
		self->val.string = NULL;
		self->list_count = 0;
		return;
	} else {
		len = strlen (value);
	}

	if (self->obj_type == X_OBJECT_TYPE_STRING) {
		if (len <= self->list_count) {
			strcpy (self->val.string, value);
			return;
		}
	}

	objptr->empty (self);
	if ((self->val.string = strdup (value)) == NULL) return;

	self->obj_type = X_OBJECT_TYPE_STRING;
	self->list_count = len;
}

static struct x_object_list_t * xobj_internal_get (
			struct x_object_t *self, const char *keystr) {
	struct x_object_list_t	*list;

	// printf ("%d\n", self->obj_type);

	if (self->obj_type != X_OBJECT_TYPE_STRING_HASH) return NULL;

	list = self->val.list;

	while (list != NULL) {
		if (list->key != NULL) {
			if (strcmp (keystr, list->key) == 0) return list;
			// fprintf (stderr, "KEY: %s\n", list->key);
		}

		list = list->next;
	}

	return NULL;
}

static char * xobj_get (struct x_object_t *self, const char *keystr) {
	struct x_object_list_t	*list = xobj_internal_get (self, keystr);
	char			*str;

	if (list == NULL) return NULL;
	// printf ("<%s>\n", (char *) list->xobj);
	if ((str = (char *) list->xobj) == NULL) return nullstr;

	return str;
}

static int xobj_internal_put (struct x_object_t *self,
				const int duplicate,
				struct x_object_t *xobj,
				char *keystr, char *value) {

	struct x_object_list_t	*list;
	int			len;

	if (keystr == NULL) return 0;

	// printf ("put (%s, %s)\n", keystr, value);

	switch (self->obj_type) {
	case X_OBJECT_TYPE_EMPTY:
		break;
	case X_OBJECT_TYPE_HASH:
		break;
	case X_OBJECT_TYPE_STRING_HASH:
		if (xobj != NULL) return 0;

		// printf ("X_OBJECT_TYPE_STRING_HASH\n");

		if ((list = xobj_internal_get (self, keystr)) != NULL) {
			// Replace ....

			len = strlen (value);

			if (list->xobj != NULL) {
				if (strlen ((void *) list->xobj) >= len) {
					memcpy (list->xobj, value, len + 1);
				} else {
					free (list->xobj);
					list->xobj = (void *) strdup (value);
					self->strlist_ok = 0;
				}
			} else {
				list->xobj = (void *) strdup (value);
				self->strlist_ok = 0;
			}
			return 1;
		}
		break;
	default:
		return 0;
	}

	if ((list = internal_new_list_obj_entry (
			xobj, keystr, value, duplicate)) == NULL) return 0;

	self->strlist_ok = 0;
	self->read_ptr = NULL;

	// printf ("put %d (%s, %s)\n", self->obj_type, keystr, value);

	if (self->obj_type == X_OBJECT_TYPE_EMPTY) {
		if (xobj != NULL) {
			self->obj_type   = X_OBJECT_TYPE_HASH;
		} else {
			self->obj_type   = X_OBJECT_TYPE_STRING_HASH;
		}
		self->val.list   = list;
		self->tail       = list;

		self->list_count = 1;
	} else if (self->obj_type == X_OBJECT_TYPE_HASH) {
		// printf ("[*] X_OBJECT_TYPE_HASH\n");
		self->tail->next = list;
		self->tail       = list;
		list->prev       = self->tail;

		++self->list_count;
	} else if (self->obj_type == X_OBJECT_TYPE_STRING_HASH) {
		// printf ("[*] X_OBJECT_TYPE_STRING_HASH\n");
		self->tail->next = list;
		self->tail       = list;
		list->prev       = self->tail;

		++self->list_count;
	}

	return 1;
}

static int xobj_put (struct x_object_t *self,
				char *keystr, char *value) {
	if (keystr == NULL) return 0;
	if (value == NULL) return 0;

	return xobj_internal_put (self, 1, NULL, keystr, value);
}

static int xobj_del (struct x_object_t *self, const char *keystr) {
	struct x_object_list_t	*list;

	if ((self->obj_type != X_OBJECT_TYPE_STRING_HASH) &&
			(self->obj_type != X_OBJECT_TYPE_HASH)) return 0;

	if ((list = xobj_internal_get (self, keystr)) != NULL) {
		if (self->read_ptr == list) self->read_ptr = NULL;

		if (self->list_count == 1) {
			self->val.list = self->tail = NULL;
			self->list_count = 0;
		} else {
			if (self->val.list == list) {
				self->val.list = list->next;
				self->val.list->prev = NULL;
			} else if (self->tail == list) {
				self->tail = list->prev;
				self->tail->next = NULL;
			}
			--self->list_count;
			free (list->key);

			if (self->obj_type == X_OBJECT_TYPE_STRING_HASH) {
				free (list->xobj);
			} else if (self->obj_type == X_OBJECT_TYPE_HASH) {
				objptr->dispose (list->xobj);
			}

			internal_free_list_obj_entry (list);
		}
	}

	return 1;
}

static int xobj_is_exist (struct x_object_t *self, const char *key) {
	if (objptr->get (self, key) != NULL) return 1;

	return 0;
}

static int xobj_get_first (struct x_object_t *self, char **key, char **value) {
	struct x_object_list_t	*list;

	if (self->obj_type != X_OBJECT_TYPE_STRING_HASH) return 0;

	self->read_ptr = NULL;
	if ((list = self->val.list) == NULL) return 0;

	*key   = list->key;
	*value = (void *) list->xobj;

	self->read_ptr	= list->next;

	return 1;
}

static int xobj_get_next (struct x_object_t *self, char **key, char **value) {
	struct x_object_list_t	*list;

	if (self->obj_type != X_OBJECT_TYPE_STRING_HASH) return 0;

	if ((list = self->read_ptr) == NULL) return 0;

	*key   = list->key;
	*value = (void *) list->xobj;

	self->read_ptr	= list->next;

	return 1;
}

static struct x_object_t * xobj_new (void) {
	struct x_object_t	*pd;

	while ((pd = malloc (sizeof (struct x_object_t))) != NULL) {
		pd->obj_type		= X_OBJECT_TYPE_EMPTY;
		pd->list_count		= 0;
		pd->strlist		= NULL;
		pd->strlist_cnt		= 0;
		pd->strlist_ok		= 0;

		object_count++;


		break;
	}

	return pd;
}

static void * xobj_cleanup (void) {
	if (autofree_intf != NULL) autofree_intf->dispose ();
	autofree_intf = NULL;
	return NULL;
}

struct x_object_interface_t *	init_x_object_interface (void) {
	if (objptr == NULL) {
		objptr = &objintf;

		objptr->newobj		= xobj_new;
		objptr->dispose		= xobj_dispose;
		objptr->type_of		= xobj_type_of;
		objptr->is_array	= xobj_is_array;
		objptr->is_empty	= xobj_is_empty;
		objptr->push		= xobj_push;
		objptr->Push		= xobj_Push;
		objptr->pop		= xobj_pop;
		objptr->shift		= xobj_shift;
		objptr->unshift		= xobj_unshift;
		objptr->Unshift		= xobj_Unshift;
		objptr->copy		= xobj_copy;
		objptr->clone		= xobj_clone;

		objptr->intval		= xobj_intval;
		objptr->realnum		= xobj_realnum;
		objptr->string		= xobj_string;
		objptr->empty		= xobj_empty;
		objptr->count		= xobj_count;

		objptr->get		= xobj_get;
		objptr->put		= xobj_put;
		objptr->del		= xobj_del;
		objptr->is_exist	= xobj_is_exist;
		objptr->get_first	= xobj_get_first;
		objptr->get_next	= xobj_get_next;

		objptr->setint		= xobj_setint;
		objptr->setreal		= xobj_setreal;
		objptr->setstr		= xobj_setstr;

		objptr->str_push	= xobj_str_push;
		objptr->str_pop		= xobj_str_pop;

		objptr->str_array	= xobj_string_array;

		objptr->cleanup		= xobj_cleanup;
	}

	if (autofree_intf == NULL) {
		autofree_intf = init_autofree_interface (120);
	}

	return objptr;
}
