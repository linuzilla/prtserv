%{
#include <stdlib.h>
#include <string.h>
#include "sys_conf.h"
#include "parser.h"

#define MAX_INT_STK_LEN		64

struct generic_stack {
	union {
		int	intval;
		char*	str;
	} x;
};

extern struct sysconf_t		*system_config_t_ptr;

static int			iftrue     = 1;
static int			ifevertrue = 0;
//static int			inifstm   = 0;
//static int			inelsestm = 0;
static int			intstk    = 1;

static int			sw_value  = 0;
static int			sw_tf     = 0;

static struct generic_stack	gstk[MAX_INT_STK_LEN];
static int			sp = 0;

static void add_tag_percent (const char *tag, const int f, const char *per) {
	char	buffer[10];
	int	len;
				
	strncpy (buffer, per, 9);

	if ((len = strlen (buffer)) > 0) {
		buffer[len-1] = '\0';
		len = atoi (buffer);
		// fprintf (stderr, "%c - %d %d\n", tag[1], f, len);
		system_config_t_ptr->add_special (tag[1], f, len);
	}
}

static void intstack_clear (void) { sp = 0; }

static int intstack_size (void) { return sp; }

static int push_int (const int value) {
	if (sp < MAX_INT_STK_LEN) {
		gstk[sp++].x.intval = value;
		return 1;
	}

	return 0;
}

static int push_str (char *str) {
	if (sp < MAX_INT_STK_LEN) {
		gstk[sp++].x.str = str;
		return 1;
	}

	return 0;
}

static int pop_int (void) {
	if (sp > 0) return gstk[--sp].x.intval;
	return 0;
}

static char *pop_str (void) {
	if (sp > 0) return gstk[--sp].x.str;
	return NULL;
}

%}

%token RW_FLAG_ON   RW_FLAG_OFF   RW_IF   RW_ELSE  RW_ELSE_IF
%token IDENTIFIER   DIGIT    QSTRING  FQSTRING
%token RW_INCREASE  RW_LOWER RW_NORMAL
%token SQ_CHARACTER PERCENT  RW_SPEICAL_STDRULE
%token RW_SWITCH    RW_CASE  RW_BREAK   RW_DEFAULT
%token RW_PRINT     RW_EXIT
%token '=' ';' ':' ',' '[' ']' '(' ')'

%%

full_definition		: system_definitions
			;

system_definitions	: statement_definitions
			| statement_definitions system_definition
			;

system_definition	: special_definition
			| special_definition variable_definitions
			;

statement_definitions	: statement_definition
			| statement_definition statement_definitions
			;

statement_definition	: variable_definition
			| ifstm_definition
			| switch_definition
			;

switch_definition	: start_sw_stm case_statements end_sw_stm
			;

start_sw_stm		: RW_SWITCH '(' IDENTIFIER ')' '{'
			  { 	
				int	val;

				// inifstm = 1;
				val = system_config_t_ptr->getint ($3);
				// fprintf (stderr, "%s=%d\n", $2, val);

				sw_value = val;
				iftrue   = 0;
				sw_tf    = 0;

				if ($3 != NULL) free ($3);
			  }
			;

end_sw_stm		: '}' { /* inifstm = 0; */ iftrue = 1; }
			;

case_statements		: case_statement
			| case_def_statement
			| case_statement case_statements
			;

case_def_statement	: default_label variable_definitions break_stm
			| case_labels default_label
			  variable_definitions break_stm
			| case_labels default_label break_stm
			| default_label break_stm
			;

default_label		: RW_DEFAULT ':' { if (! sw_tf) iftrue = 1; }
			;

case_statement		: case_labels variable_definitions
			| case_labels variable_definitions break_stm
			| case_labels break_stm
			;

break_stm		: RW_BREAK ';' { iftrue = 0; }
			;

case_labels		: case_label
			| case_label case_labels
			;

case_label		: RW_CASE DIGIT ':'
			{ if (sw_value == atoi ($2)) iftrue = sw_tf = 1; }
			;

ifstm_definition	: start_if_stm variable_definitions
			  start_elif_stm variable_definitions
			  start_else_stm variable_definitions stop_else_stm
			| start_if_stm variable_definitions
			  start_else_stm variable_definitions stop_else_stm
			| start_if_stm variable_definitions stop_if_stm
			;

start_if_stm		: RW_IF '(' IDENTIFIER ')' '{'
			  { 	
				int	val;

				// inifstm = 1;
				val = system_config_t_ptr->getint ($3);
				// fprintf (stderr, "%s=%d\n", $2, val);

				ifevertrue = (iftrue = (val > 0) ? 1 : 0);

				if ($3 != NULL) free ($3);
			  }
			;

stop_if_stm		: '}' { /* inifstm = 0; */  iftrue = 1; }
			;

start_elif_stm		: '}' RW_ELSE_IF '(' IDENTIFIER ')' '{'
			  {
				if (! ifevertrue) {
					int	val;
					// inifstm = 1;
					val = system_config_t_ptr->getint ($3);
					// fprintf (stderr, "%s=%d\n", $2, val);

					iftrue = (val > 0) ? 1 : 0;
	
					if ($3 != NULL) free ($3);

					ifevertrue = iftrue;
				} else {
					iftrue = 0;
				}
			  }
			;

start_else_stm		: '}' RW_ELSE '{' { iftrue = ! ifevertrue; }
			;

stop_else_stm		: '}' {
				/* inelsestm = 0; */ iftrue = 1;
				ifevertrue = 0;
			   }
			;

variable_definitions	: variable_definition
			| variable_definition variable_definitions
			;

variable_definition	: variable_statement
			| print_statement
			| exit_statement
			;

exit_statement		: RW_EXIT ';' { if (iftrue) exit (0); }
			;

print_statement		: RW_PRINT QSTRING ';'
			{
				int	i, j, k, len;
				char	*ptr;
				char	c;

				if (iftrue) {
					ptr = $2;
					len = strlen (ptr);

					for (i = j = k = 0; i < len; i++) {
						c = ptr[i];

						if (k == 1) {
							switch (c) {
							case 'n':
								ptr[j++] = '\n';
								break;
							case 'r':
								ptr[j++] = '\r';
								break;
							case '\\':
								ptr[j++] = '\\';
								break;
							}
							k = 0;
						} else if (c == '\\') {
							k = 1;
						} else {
							ptr[j++] = c;
						}
					}
					ptr[j] = '\0';

					fprintf (stderr, "%s", ptr);
				}

				free ($2);
			}
			;

variable_statement	: IDENTIFIER '=' DIGIT ';'
			{
				if (iftrue) {
					system_config_t_ptr->addint ($1, $3);
				}

				if ($1 != NULL) free ($1);
				if ($3 != NULL) free ($3);
			}
			| IDENTIFIER '=' QSTRING ';'
			{
				if (iftrue) {
					system_config_t_ptr->addstr ($1, $3);
				}

				if ($1 != NULL) free ($1);
				if ($3 != NULL) free ($3);
			}
			| IDENTIFIER '=' RW_FLAG_ON  ';'
			{
				if (iftrue) {
					system_config_t_ptr->addflag_on ($1);
				}

				if ($1 != NULL) free ($1);
			}
			| IDENTIFIER '=' RW_FLAG_OFF ';'
			{
				if (iftrue) {
					system_config_t_ptr->addflag_off ($1);
				}
				if ($1 != NULL) free ($1);
			}
			| IDENTIFIER '=' '[' comma_list_data ']' ';'
			{
				int	i, len;

				// fprintf (stderr, "iftrue=%d\n", iftrue);

				if (iftrue && (intstk == 1)) {
					int	*list;

					list = calloc ((len = intstack_size ()),
						sizeof (int));
				
					if (list != NULL) {
						for (i = 0; i < len; i++) {
							list[i] = pop_int ();
						}
					}

					// fprintf (stderr, "int list\n");
					system_config_t_ptr->add_int_list ($1,
								list, len);
				} else if (iftrue && (intstk == 0)) {
					char	**list;

					list = calloc ((len = intstack_size ()),
						sizeof (char *));

					if (list != NULL) {
						for (i = 0; i < len; i++) {
							list[i] = pop_str ();
						}
					}

					// fprintf (stderr, "string list\n");
					system_config_t_ptr->add_str_list ($1,
								list, len);
				}

				intstack_clear ();

				if ($1 != NULL) free ($1);
			}
			;

comma_list_data		: comma_list_number { intstk = 1; }
			| comma_list_string { intstk = 0; }
			;

comma_list_number	: DIGIT
			{
				if (iftrue) push_int (atoi ($1));
				if ($1 != NULL) free ($1);
			}
			| DIGIT ',' comma_list_number
			{
				if (iftrue) push_int (atoi ($1));
				if ($1 != NULL) free ($1);
			}
			;

comma_list_string	: QSTRING
			{ if (iftrue) push_str ($1); }
			| QSTRING ',' comma_list_string
			{ if (iftrue) push_str ($1); }
			;

special_definition	: RW_SPEICAL_STDRULE '{' tag_definitions '}' ;

tag_definitions		: tag_definition
			| tag_definition tag_definitions
			;

tag_definition		: RW_FLAG_ON SQ_CHARACTER RW_INCREASE PERCENT ';'
			{
				add_tag_percent ($2, 1, $4);
				free ($2);
				free ($4);
			}
			| RW_FLAG_ON  SQ_CHARACTER RW_LOWER    PERCENT ';'
			{
				add_tag_percent ($2, 2, $4);
				free ($2);
				free ($4);
			}
			| RW_FLAG_ON SQ_CHARACTER RW_NORMAL ';'
			{
				add_tag_percent ($2, 3, "0%");
				free ($2);
			}
			;

%%

int yywrap (void) { return 1; }
