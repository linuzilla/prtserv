/*
 *	sys_conf.c
 *
 *	Copyright (c) 2002, Written by Jiann-Ching Liu
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "sys_conf.h"


struct sysconf_entry_t {
        char                    *key;
        int                     ivalue;
       	char			*value;
	int			*list;
	char			**slist;
        struct sysconf_entry_t  *next;
};

#define MAX_SPECIAL_STUDENT_RULES_ENTRY	127

struct special_student_rules_t {
	int	flag;
	int	percent;
};

struct sysconf_t		*system_config_t_ptr = NULL;
static const char		*default_cfg_dir = "/usr/local/etc";
//static char			*null_string = NULL;

static struct special_student_rules_t	sstru[MAX_SPECIAL_STUDENT_RULES_ENTRY];

static struct sysconf_entry_t	  sysconf_entry = {NULL, -1, NULL, NULL, NULL};
static struct sysconf_entry_t	* sysconf_ptr (const char *key);
static struct sysconf_entry_t	* sysconf_key_pointer = NULL;

static void*	addentry_integer(const char *entry, const char *value);
static void*	addentry_int (const char *entry, const int value);
static void*	addentry_string	(const char *entry, const char *value);
static void*	addentry_flag_on  (const char *entry);
static void*	addentry_flag_off (const char *entry);
static void*	addentry_intlist (const char *key, int *list, int len);
static void*	addentry_strlist (const char *key, char **list, int len);
static char	*strip_qstring	(const char *qst);

extern FILE	*yyin;


static char * sysconf_str (const char *key) {
        struct sysconf_entry_t  *ptr;

        if ((ptr = sysconf_ptr (key)) != NULL) return ptr->value;
        return NULL;
}

static int sysconf_int (const char *key) {
        struct sysconf_entry_t  *ptr;

        if ((ptr = sysconf_ptr (key)) != NULL) return ptr->ivalue;
        return -1;
}

static int* sysconf_get_int_list (const char *key, int *len) {
        struct sysconf_entry_t  *ptr;
	int			length = 0;
	int			*list = NULL;

        if ((ptr = sysconf_ptr (key)) != NULL) {
		list   = ptr->list;
		length = ptr->ivalue;
	}

	if (len != NULL) *len = length;

	return list;
}

static char** sysconf_get_str_list (const char *key, int *len) {
        struct sysconf_entry_t  *ptr;
	int			length = 0;
	char			**list = NULL;

        if ((ptr = sysconf_ptr (key)) != NULL) {
		list   = ptr->slist;
		length = ptr->ivalue;
	}

	if (len != NULL) *len = length;

	return list;
}

static char * sysconf_get_first_key (void) {
	if ((sysconf_key_pointer = sysconf_entry.next) == NULL) return NULL;

	return sysconf_key_pointer->key;
}

static char * sysconf_get_next_key (void) {
	if (sysconf_key_pointer == NULL) return NULL;

	if ((sysconf_key_pointer = sysconf_key_pointer->next) == NULL)
		return NULL;

	return sysconf_key_pointer->key;
}

static int sysconf_add_special (const char key, const int flag, const int pc) {
	int	i;

	i = (int) key;

	if ((i >= 0) && (i < MAX_SPECIAL_STUDENT_RULES_ENTRY)) {
		sstru[i].flag    = flag;
		sstru[i].percent = pc;

#if IGNORE_CASE_ON_SPECIAL == 1
		if ((key >= 'A') && (key <= 'Z')) {
			i = (int) (key - 'A' + 'a');

			sstru[i].flag    = flag;
			sstru[i].percent = pc;
		} else if ((key >= 'a') && (key <= 'z')) {
			i = (int) (key - 'a' + 'A');

			sstru[i].flag    = flag;
			sstru[i].percent = pc;
		}
#endif
	}

	return 0;
}

static int sysconf_get_special (const char key, int *percent) {
	int	i;

	i = (int) key;

	if ((i >= 0) && (i < MAX_SPECIAL_STUDENT_RULES_ENTRY)) {
		*percent = sstru[i].percent;
		return sstru[i].flag;
	}

	return 0;
}

struct sysconf_t * initial_sysconf_module (char *file,
				const char *variable, const int value) {
	static struct sysconf_t	syscnf;
	static short		initialized = 0;
	int			i, result;
	char			*cfgfile;

	if (! initialized) {
		syscnf.getstr		= sysconf_str;
		syscnf.getint		= sysconf_int;
		syscnf.intlist		= sysconf_get_int_list;
		syscnf.strlist		= sysconf_get_str_list;
		syscnf.first_key	= sysconf_get_first_key;
		syscnf.next_key		= sysconf_get_next_key;
		syscnf.addint   	= addentry_integer;
		syscnf.addint_x   	= addentry_int;
		syscnf.addstr		= addentry_string;
		syscnf.addflag_on	= addentry_flag_on;
		syscnf.addflag_off	= addentry_flag_off;
		syscnf.add_int_list	= addentry_intlist;
		syscnf.add_str_list	= addentry_strlist;
		syscnf.add_special	= sysconf_add_special;
		syscnf.get_special	= sysconf_get_special;

		system_config_t_ptr = &syscnf;

		for (i = 0; i < MAX_SPECIAL_STUDENT_RULES_ENTRY; i++) {
			sstru[i].flag = 0;
		}

		initialized = 1;
	}

	if (file != NULL) {
		if (access (file, R_OK) == 0) {
			cfgfile = file;
		} else if (strchr (file, '/') == NULL) {
			cfgfile = malloc (strlen (file) +
					strlen (default_cfg_dir) + 2);

			sprintf (cfgfile, "%s/%s",
					default_cfg_dir, file);

			file = cfgfile;
		}

		// fprintf (stderr,
		//	"Reading configuration file [ %s ] ... ", file);

		syscnf.addint_x (variable, value);

		if ((yyin = fopen (file, "r")) != NULL) {
			// fprintf (stderr, "ok\n\n");

			result = yyparse ();
			fclose (yyin);

			if (result != 0) {
				fprintf (stderr, "%s: parsing error\n", file);
				return NULL;
			}
		} else {
			perror (file);
			return NULL;
		}
	}

	return &syscnf;
}

////////////////////////////////////////////////////////////

static void* addentry_int (const char *entry, const int value) {
        struct sysconf_entry_t  *ptr;

        if ((ptr = addentry_string (entry, NULL)) != NULL) ptr->ivalue = value;

        return ptr;
}

static void* addentry_integer (const char *entry, const char *value) {
        int                     data;
//      struct sysconf_entry_t  *ptr;

        data = atoi (value);

//      if ((ptr = addentry_string (entry, value)) != NULL) ptr->ivalue = data;

//      return ptr;
	return addentry_int (entry, data);
}

static void* addentry_string (const char *entry, const char *value) {
        struct sysconf_entry_t  *ptr;
	char			*str;

        if ((ptr = sysconf_ptr (entry)) == NULL) {
        	if ((ptr = malloc (sizeof (struct sysconf_entry_t))) != NULL) {
			ptr->key    = strdup (entry);
                	ptr->next   = sysconf_entry.next;

	                sysconf_entry.next = ptr;
		}
	} else {
		if (ptr->value != NULL) {
			// fprintf (stderr, "Free value: %s\n", ptr->value);
			free (ptr->value);
		}
		// if (ptr->list  != NULL) free (ptr->list);
		// if (ptr->slist != NULL) free (ptr->list);
	}

        if (ptr != NULL) {
		if (value == NULL) {
			//ptr->value  = null_string;
			ptr->value  = NULL;
		} else if (value[0] == '"') {
			str = strip_qstring(value);

			if (strlen (str) == 0) {
				ptr->value = NULL;
			} else {
				ptr->value = strdup (str);
			}
                        // ptr->value  = strdup (strip_qstring(value));
                } else {
                        ptr->value  = strdup (value);
                }

		// fprintf (stderr, "%s=%s\n", ptr->key, ptr->value);
                ptr->ivalue = -1;
		ptr->list   = NULL;
		ptr->slist  = NULL;
        }

        return ptr;
}

static void* addentry_flag_on (const char *entry) {
        struct sysconf_entry_t  *ptr;

        if ((ptr = addentry_string (entry, NULL)) != NULL) ptr->ivalue = 1;

        return ptr;
}

static void* addentry_flag_off (const char *entry) {
        struct sysconf_entry_t  *ptr;

        if ((ptr = addentry_string (entry, NULL)) != NULL) ptr->ivalue = 0;

        return ptr;
}

static void* addentry_intlist (const char *key, int *list, int len) {
        struct sysconf_entry_t  *ptr;

        if ((ptr = addentry_string (key, NULL)) != NULL) {
		ptr->ivalue = len;
		ptr->list   = list;
	}

	return ptr;
}

static void* addentry_strlist (const char *key, char **list, int len) {
        struct sysconf_entry_t  *ptr;

        if ((ptr = addentry_string (key, NULL)) != NULL) {
		ptr->ivalue = len;
		ptr->slist  = list;
	}

	return ptr;
}


static char *strip_qstring (const char *qst) {
        static char     buffer[4096];
        int             len;

        buffer[0] ='\0';
        strncpy (buffer, &qst[1], 4095);
        if ((len = strlen (buffer)) >= 1) buffer[len-1] = '\0';

        return buffer;
}

static struct sysconf_entry_t * sysconf_ptr (const char *key) {
        struct sysconf_entry_t  *ptr = &sysconf_entry;

        while ((ptr = ptr->next) != NULL) {
                if (strcasecmp (key, ptr->key) == 0) {
                        return ptr;
                }
        }
        return NULL;
}
