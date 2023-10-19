/*
 *	parser.h
 *
 *	Copyright (c) 2002, Jiann-Ching Liu
 */

#ifndef __PARSER_H_
#define __PARSER_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include <sys/types.h>
#include <stdio.h>

typedef char *  char_ptr;
#define YYSTYPE char_ptr
#define YY_NO_UNPUT

extern void	yyerror (const char *);
extern int	yyparse (void);
extern int      yylex   (void);

#if defined(__cplusplus)
}
#endif

#endif
