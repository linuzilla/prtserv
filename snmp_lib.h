/*
 *	snmp_lib.h
 *
 *	Copyright (c) 2004, Jiann-Ching Liu
 */

#ifndef __SNMP_LIB_H__
#define __SNMP_LIB_H__

struct snmp_lib_pd_t;

struct snmp_lib_t {
	struct snmp_lib_pd_t	*pd;

	char *		(*snmpget)(struct snmp_lib_t *, 
				char *host, const int port,
				char *community, const char *objid);
	void		(*set)(struct snmp_lib_t *, 
				const char *host,
				const int port, const char *community);
	void		(*dispose)(struct snmp_lib_t *);
	unsigned int	(*get_int32)(struct snmp_lib_t *, const char *objid);
	char *		(*get_str)(struct snmp_lib_t *, const char *objid);
	unsigned int	(*int32)(const char *str);
	char *		(*string)(char *str);
};

struct snmp_lib_t	*new_snmp_lib (void);

#endif
