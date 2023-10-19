/*
 *	snmp_lib.c
 *
 *	Copyright (c) 2004, Jiann-Ching Liu
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/utilities.h>
#include <net-snmp/net-snmp-includes.h>
#include <stdio.h>
#include <stdlib.h>
#include "snmp_lib.h"

struct snmp_lib_pd_t {
	netsnmp_session	session;
	char		*host;
	char		*community;
	int		port;
};

/*
st=1   snmpget() - query an agent and return a single value.
st=2   snmpgetnext() - query an agent and return the next single value.
st=3   snmpwalk() - walk the mib and return a single dimensional array
          containing the values.
st=4 snmprealwalk() and snmpwalkoid() - walk the mib and return an
          array of oid,value pairs.
st=5-8 ** Reserved **
st=11  snmpset() - query an agent and set a single value
*/

/*
static int snl_snmpgetnext () { return 0; }
static int snl_snmpwalk () { return 0; }
static int snl_snmprealwalk () { return 0; }
static int snl_snmpset() { return 0; }
*/

#define NUM_OF_BUFFER		(12)
#define BUFFER_SIZE		(1024)

static char * snl_string (char *str) {
	const char	xstr[] = "STRING: ";

	if (strncasecmp (str, xstr, sizeof xstr - 1) == 0) {
		return &str[sizeof xstr - 1];
	}

	return NULL;
}

static unsigned int snl_int32 (const char *str) {
	const char	xstr[] = "Counter32: ";

	if (strncasecmp (str, xstr, sizeof xstr - 1) == 0) {
		return atoi (&str[sizeof xstr - 1]);
	}

	return 0;
}


static void snl_set (struct snmp_lib_t *self,
					const char *host, const int port,
					const char *community) {
	struct snmp_lib_pd_t	*pd = self->pd;


	pd->host	= strdup (host);
	pd->community	= strdup (community);
	pd->port	= port;
}

static unsigned int snl_get_int32 (
		struct snmp_lib_t *self, const char *objid) {

	struct snmp_lib_pd_t	*pd = self->pd;
	char			*ptr;

	if ((ptr = self->snmpget (self, pd->host,
				pd->port, pd->community, objid)) != NULL) {
		return self->int32 (ptr);
	}

	return 0;
}

static char * snl_get_str (
		struct snmp_lib_t *self, const char *objid) {
	struct snmp_lib_pd_t	*pd = self->pd;
	char			*ptr;

	if ((ptr = self->snmpget (self, pd->host,
				pd->port, pd->community, objid)) != NULL) {
		return self->string (ptr);
	}

	return 0;
}

static char * snl_snmpget (struct snmp_lib_t *self,
					char *host, const int port,
					char *community,
					const char *objid) {
	struct snmp_lib_pd_t	*pd = self->pd;
	netsnmp_session		*ss;
	netsnmp_pdu		*pdu;
	netsnmp_pdu		*response = NULL;
	size_t			name_length;
	oid			name[MAX_OID_LEN];
	int			i, status;
	netsnmp_variable_list	*vars;
	static int		bufidx = 1;
	static char		buffer[NUM_OF_BUFFER][BUFFER_SIZE];
	char			*ptr = NULL;


	pd->session.peername    = host;
	pd->session.remote_port = port;
	pd->session.version	= SNMP_VERSION_1;

	pd->session.community	= community;
	pd->session.community_len = strlen (community);

	pd->session.authenticator = NULL;
	// pd->session.retries	= 3;
	// pd->session.timeout	= 


	if ((ss = snmp_open (&pd->session)) == NULL) {
		snmp_sess_perror ("snmp_lib", &pd->session);
		return 0;
	}

	pdu = snmp_pdu_create (SNMP_MSG_GET);

	name_length = MAX_OID_LEN;

	if (! snmp_parse_oid (objid, name, &name_length)) {
		snmp_perror (objid);
		snmp_close (ss);
		return NULL;
	} else {
		snmp_add_null_var (pdu, name, name_length);
	}

	status = snmp_synch_response (ss, pdu, &response);

	if (status == STAT_SUCCESS) {
		if (response->errstat == SNMP_ERR_NOERROR) {
			for (vars = response->variables; vars;
					vars = vars->next_variable) {

				bufidx = (bufidx + 1) % NUM_OF_BUFFER;
				ptr = buffer[bufidx];

				// snprint_variable (
				snprint_value (ptr, BUFFER_SIZE,
						vars->name,
						vars->name_length, vars);

				ptr[BUFFER_SIZE-1] = '\0';
				// fprintf (stderr,"[%s]\n", ptr);
			}
		} else {
			fprintf (stderr, "Error in packet\nReason: %s\n",
				snmp_errstring (response->errstat));

			if (response->errindex != 0) {
				fprintf (stderr, "Failed object: ");

				for (i = 1, vars = response->variables;
					vars && i != response->errindex;
					vars = vars->next_variable, i++)
					/*EMPTY*/;

				if (vars) {
					fprint_objid (stderr,
							vars->name,
							vars->name_length);
				}
				fprintf(stderr, "\n");
			}
		}
	} else if (status == STAT_TIMEOUT) {
		fprintf (stderr, "Timeout: No Response from %s.\n",
						pd->session.peername);
	} else {
		snmp_sess_perror ("snmp_lib:snmpget", ss);
	}

	if (response) snmp_free_pdu (response);

	snmp_close (ss);

	return ptr;
}


static void snl_dispose (struct snmp_lib_t *self) {
	if (self->pd != NULL) {
		if (self->pd->host != NULL) free (self->pd->host);
		if (self->pd->community != NULL) free (self->pd->community);
		free (self->pd);
	}
	free (self);
}

struct snmp_lib_t *new_snmp_lib (void) {
	struct snmp_lib_t	*self = NULL;
	struct snmp_lib_pd_t	*pd;

	if ((self = malloc (sizeof *self)) != NULL) {
		if ((pd = malloc (sizeof *pd)) == NULL) {
			free (self);
			return NULL;
		}

		self->pd = pd;

		self->snmpget	= snl_snmpget;
		self->dispose	= snl_dispose;

		self->int32	= snl_int32;
		self->string	= snl_string;

		self->set	= snl_set;
		self->get_int32	= snl_get_int32;
		self->get_str	= snl_get_str;

		// --------------------------------------------------
		
		pd->host	= NULL;
		pd->community	= NULL;
		pd->port	= 161;

		snmp_sess_init (&pd->session);
		pd->session.peername  = NULL;
		pd->session.community = NULL;

		snmp_enable_stderrlog ();
		init_snmp ("snmp_lib");
	}

	return self;
}
