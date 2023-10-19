/*
 *	db_mysql.c
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <mysql/mysql.h>
#include "db_mysql.h"
#include "x_object.h"

struct db_mysql_pd_t {
	MYSQL			*mysql_conn;
	MYSQL_RES		*res_ptr;
	MYSQL_ROW		sqlrow;
	short			connect_ok;
	short			new_query;
	short			verbose;
	short			have_newfield;
	short			print_query;
	int			res;
	unsigned int		rows;
	unsigned int		fields;
	int			aff_rows;
	unsigned int		mysqlsvr_port;
	char			*mysqlsvr_unix_socket;
	unsigned int		mysqlsvr_clientflag;
	struct x_object_t	*result_object;
	struct x_object_t	*field_object;
	char			*my_default_db;
};

static struct x_object_interface_t	*objintf = NULL;

static int dbmysql_connect (struct db_mysql_t *self,
			const char *server, const char *account,
			const char *passwd, const char *database) {
	struct db_mysql_pd_t	*pd = self->pd;

	if (pd->verbose > 2) {
		fprintf (stderr, "Connect to MySQL Server ... ");
	}

	self->disconnect (self);

	if (! (pd->mysql_conn = mysql_init (NULL))) {
		fprintf (stderr, "mysql_init: failed !\n");
		return 0;
	}

	if (! (pd->mysql_conn = mysql_real_connect (pd->mysql_conn,
				server, account, passwd, database,
				pd->mysqlsvr_port,
				pd->mysqlsvr_unix_socket,
				pd->mysqlsvr_clientflag))) {
		if (pd->verbose > 1) {
			fprintf (stderr, "Connect to mysql server failed\n");
		}
		return 0;
	}
	pd->connect_ok = 1;

	if (pd->verbose > 2) {
		fprintf (stderr, "%s/%s [%s]\n",
				mysql_get_server_info (pd->mysql_conn),
				mysql_get_client_info (),
				mysql_get_host_info (pd->mysql_conn));
		if (pd->verbose > 3) {
			fprintf (stderr, "%s\n",
				mysql_stat (pd->mysql_conn));
		}
	}

	self->pd->my_default_db = strdup (database);

	return 1;
}

static void dbmysql_unix_socket (struct db_mysql_t *self, const char *usock) {
	struct db_mysql_pd_t	*pd = self->pd;

	if (pd->mysqlsvr_unix_socket != pd->mysqlsvr_unix_socket) {
		free (pd->mysqlsvr_unix_socket);
		pd->mysqlsvr_unix_socket = NULL;
	}

	if (usock != NULL) pd->mysqlsvr_unix_socket = strdup (usock);
}

static void dbmysql_set_port (struct db_mysql_t *self, const int port) {
	struct db_mysql_pd_t	*pd = self->pd;

	pd->mysqlsvr_port = port;
}

static void dbmysql_disconnect (struct db_mysql_t *self) {
	struct db_mysql_pd_t	*pd = self->pd;

	if (pd->connect_ok) {
		if (pd->verbose > 1) {
			fprintf (stderr, "Disconnect mysql server ... ");
		}

		mysql_close (pd->mysql_conn);
		pd->connect_ok = 0;

		if (pd->verbose > 1) fprintf (stderr, "ok\n");
	}
}

static unsigned long long dbmysql_insert_id (struct db_mysql_t *self) {
	return mysql_insert_id (self->pd->mysql_conn);
}

static void dbmysql_print_query (struct db_mysql_t *self, const int yn) {
	self->pd->print_query = yn;
}

static int dbmysql_free_result (struct db_mysql_t *self) {
	struct db_mysql_pd_t	*pd = self->pd;

	if (pd->res_ptr != NULL) {
		mysql_free_result (pd->res_ptr);
		pd->res_ptr = NULL;
		return 1;
	}

	return 0;
}

static int dbmysql_query (struct db_mysql_t *self, const char *query, ...) {
	struct db_mysql_pd_t	*pd = self->pd;
	int			noa;
	char			qstr[8192];
	va_list			ap;

	va_start (ap, query);
	vsnprintf (qstr, sizeof qstr - 1, query, ap);
	va_end (ap);

	if (pd->print_query) fprintf (stderr, "[%s]\n", qstr);

	pd->rows   = 0;
	pd->fields = 0;
	pd->have_newfield = 1;

	self->store_result (self);
	self->free_result (self);

	pd->res = mysql_query (pd->mysql_conn, qstr);

	if (pd->res) return 0;

	pd->aff_rows = noa = mysql_affected_rows (pd->mysql_conn);

	if (pd->verbose >= 5) {
		if (noa > 0) {
			fprintf (stderr, "%d rows affected\n", noa);
		}
	}

	pd->new_query = 1;

	return 1;
}

static int dbmysql_affected_rows (struct db_mysql_t *self) {
	return self->pd->aff_rows;
}

static int dbmysql_perror (struct db_mysql_t *self, const char *str) {
	struct db_mysql_pd_t	*pd = self->pd;

	if (pd->res) {
		fprintf (stderr, "%s: %s\n", str, mysql_error (pd->mysql_conn));
		return 1;
	}

	return 0;
}

static int dbmysql_store_result (struct db_mysql_t *self) {
	struct db_mysql_pd_t	*pd = self->pd;

	if (pd->new_query) {
		pd->new_query = 0;

		self->free_result (self);

		if ((pd->res_ptr = mysql_store_result (
						pd->mysql_conn)) == NULL) {
			pd->rows = pd->fields = 0;
			return 0;
		}

		pd->rows   = (unsigned int) mysql_num_rows (pd->res_ptr);
		pd->fields = (unsigned int) mysql_num_fields (pd->res_ptr);

		/*
		fprintf (stderr, "%u rows, %u fields\n",
					pd->rows, pd->fields);
		*/
	}

	return 1;
}

static char ** dbmysql_fetch_fields (struct db_mysql_t *self) {
	struct db_mysql_pd_t	*pd = self->pd;
	MYSQL_FIELD		*fptr;
	int			i, fcnt = 0, nof;
	char			field_name_buffer[20];

	if (pd->have_newfield) {
		self->store_result (self);
		pd->have_newfield = 0;

		objintf->empty (pd->field_object);

		nof = self->num_fields (self);

		for (i = 0; i < nof; i++) {
			if ((fptr = mysql_fetch_field_direct (
						pd->res_ptr, i)) == NULL) {
				break;
			}
			fcnt++;
			if (fptr->name == NULL) {
				sprintf (field_name_buffer, "unnamed_%d", fcnt);
				objintf->str_push (pd->field_object,
							field_name_buffer);
				// fprintf (stderr, "%s\n", field_name_buffer);
			} else {
				objintf->str_push (pd->field_object,
								fptr->name);
				// fprintf (stderr, "%s\n", fptr->name);
			}
		}
	}

	return objintf->str_array (pd->field_object);
}

static unsigned int dbmysql_num_rows (struct db_mysql_t *self) {
	self->store_result (self);

	return self->pd->rows;
}

static unsigned int dbmysql_num_fields (struct db_mysql_t *self) {
	self->store_result (self);

	return self->pd->fields;
}

static char ** dbmysql_fetch (struct db_mysql_t *self) {
	struct db_mysql_pd_t	*pd = self->pd;
	struct x_object_t	*xobj;
	int			i, num_fields;


	self->store_result (self);

	if (pd->res_ptr == NULL) return NULL;

	xobj = pd->result_object;

	objintf->empty (xobj);

	if (! (pd->sqlrow = mysql_fetch_row (pd->res_ptr))) {
		return NULL;
	}

	num_fields = self->num_fields (self);

	for (i = 0; i < num_fields; i++) {
		objintf->str_push (xobj, pd->sqlrow[i]);
	}

	return objintf->str_array (xobj);
}

static struct x_object_t * dbmysql_fetch_array (struct db_mysql_t *self) {
	struct db_mysql_pd_t	*pd = self->pd;
	struct x_object_t	*xobj;
	int			i, num_fields;
	char			**flist;


	self->store_result (self);

	if (pd->res_ptr == NULL) return NULL;

	xobj = pd->result_object;

	objintf->empty (xobj);

	if (! (pd->sqlrow = mysql_fetch_row (pd->res_ptr))) {
		return NULL;
	}

	num_fields = self->num_fields (self);

	flist = self->fetch_fields (self);

	for (i = 0; i < num_fields; i++) {
		objintf->put (xobj, flist[i], pd->sqlrow[i]);
	}

	return xobj;
}

static int dbmysql_select_db (struct db_mysql_t *self, const char *database) {
	const char	*dbname = NULL;

	if (database == NULL) {
		if (self->pd->my_default_db != NULL) {
			dbname = self->pd->my_default_db;
		}
	} else {
		dbname = database;
	}

	if (mysql_select_db (self->pd->mysql_conn, dbname) == 0) return 1;

	return 0;
}

static int dbmysql_verbose (struct db_mysql_t *self, const int vlevel) {
	int	rc = self->pd->verbose;

	self->pd->verbose = vlevel;

	return rc;
}

static void dbmysql_dispose (struct db_mysql_t *self) {
	objintf->dispose (self->pd->result_object);
	objintf->dispose (self->pd->field_object);

	self->free_result (self);
	self->disconnect (self);
	self->unix_socket (self, NULL);

	if (self->pd->my_default_db != NULL) free (self->pd->my_default_db);

	free (self->pd);
	free (self);
}

struct db_mysql_t * new_dbmysql (void) {
	struct db_mysql_t	*self;
	struct db_mysql_pd_t	*pd;

	if (objintf == NULL) objintf = init_x_object_interface ();

	while ((self = malloc (sizeof (struct db_mysql_t))) != NULL) {
		if ((self->pd = pd = malloc (
				sizeof (struct db_mysql_pd_t))) == NULL) {
			free (self);
			self = NULL;
			break;
		}

		if ((pd->result_object = objintf->newobj ()) == NULL) {
			free (self->pd);
			free (self);
			return NULL;
		}

		if ((pd->field_object = objintf->newobj ()) == NULL) {
			objintf->dispose (pd->result_object);
			free (self->pd);
			free (self);
			return NULL;
		}

		self->connect		= dbmysql_connect;
		self->disconnect	= dbmysql_disconnect;
		self->perror		= dbmysql_perror;
		self->query		= dbmysql_query;
		self->fetch		= dbmysql_fetch;
		self->free_result	= dbmysql_free_result;
		self->dispose		= dbmysql_dispose;
		self->unix_socket	= dbmysql_unix_socket;
		self->set_port		= dbmysql_set_port;
		self->num_rows		= dbmysql_num_rows;
		self->num_fields	= dbmysql_num_fields;
		self->store_result	= dbmysql_store_result;
		self->verbose		= dbmysql_verbose;
		self->select_db		= dbmysql_select_db;
		self->fetch_fields	= dbmysql_fetch_fields;
		self->fetch_array	= dbmysql_fetch_array;
		self->insert_id		= dbmysql_insert_id;
		self->affected_rows	= dbmysql_affected_rows;
		self->print_query	= dbmysql_print_query;

		pd->connect_ok		= 0;
		pd->res			= 0;
		pd->new_query		= 0;
		pd->verbose		= 3;
		pd->have_newfield	= 0;
		pd->res_ptr		= NULL;

		pd->mysqlsvr_port		= 0;
		pd->mysqlsvr_unix_socket	= NULL;
		pd->mysqlsvr_clientflag		= 0;

		pd->my_default_db		= NULL;

		pd->rows			= 0;
		pd->fields			= 0;

		pd->print_query			= 0;


		break;
	}

	return self;
}
