%{

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "parser.h"
#include "y.tab.h"

static int     lineno = 1;
static char    linebuf[512];

%}

ws		[ \t]+
comment		\/\/.*|#.*
id		[_a-zA-Z][\-_a-zA-Z0-9]*
string		\"[^\"\n]*\"

%%

{ws}			/* do nothing */ ;
{comment}		/* do nothing */ ;
^.*			{ strncpy (linebuf, yytext, 511); yyless(0); }
\n			{ lineno++;			}
\x1a|\r			{ /* *** ignore *** */		}
if			{ return RW_IF;			}
else			{ return RW_ELSE;		}
else-if			{ return RW_ELSE_IF;		}
on			{ return RW_FLAG_ON;		}
off			{ return RW_FLAG_OFF;		}
special-student-rules	{ return RW_SPEICAL_STDRULE;	}
increase		{ return RW_INCREASE;		}
lower			{ return RW_LOWER;		}
normal			{ return RW_NORMAL;		}
switch			{ return RW_SWITCH;		}
case			{ return RW_CASE;		}
break			{ return RW_BREAK;		}
default			{ return RW_DEFAULT;		}
print			{ return RW_PRINT;		}
exit			{ return RW_EXIT;		}
\'[\a-zA-Z0-9]\'	{ yylval = strdup (yytext); return SQ_CHARACTER; }
[0-9]{1,3}\%		{ yylval = strdup (yytext); return PERCENT;	 }
{id}			{ yylval = strdup (yytext); return IDENTIFIER; }
{string}		{
				int	len;

				len = strlen (yytext);

				if (len == 2) {
					// fprintf (stderr, "empty\n");
					// yylval = strdup (yytext);
					yylval = NULL;
				} else {
					yylval = malloc (len - 1);

					if (yylval != NULL) {
						strncpy (yylval,
							&yytext[1], len-2);
						yylval[len-2] = '\0';
					} else {
						fprintf (stderr, "memory\n");
						yylval = strdup (yytext);
					}
				}
				return QSTRING;
			}
[0-9]+			{ yylval = strdup (yytext); return DIGIT;      }
.			{ return yytext[0];       }

%%

void yyerror(const char *msg) {
	fprintf (stderr, "\nline %d: %s: %s\n", lineno, msg, linebuf);
}
