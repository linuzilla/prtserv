/*
 *	db_mysql.h
 *
 *	Copyright (c) 2003, Jiann-Ching Liu
 */

#ifndef __DB_MYSQL_H__
#define __DB_MYSQL_H__

struct db_mysql_pd_t;
struct x_object_t;

struct db_mysql_t {
	struct db_mysql_pd_t	*pd;

	void	(*unix_socket)(struct db_mysql_t *self, const char *usock);
	void	(*set_port)(struct db_mysql_t *self, const int port);
	int	(*query)(struct db_mysql_t *self, const char *query, ...);
	char ** (*fetch)(struct db_mysql_t *self);
	int	(*store_result)(struct db_mysql_t *self);
	int	(*verbose)(struct db_mysql_t *self, const int vlevel);
	int	(*select_db)(struct db_mysql_t *self, const char *newdb);
	struct x_object_t *	(*fetch_array)(struct db_mysql_t *self);
	char **	(*fetch_fields)(struct db_mysql_t *self);
	unsigned int	(*num_rows)(struct db_mysql_t *self);
	unsigned int	(*num_fields)(struct db_mysql_t *self);
	int	(*free_result)(struct db_mysql_t *self);
	int	(*perror)(struct db_mysql_t *self, const char *str);
	int	(*connect)(struct db_mysql_t *self,
				const char *server, const char *account,
				const char *passwd, const char *database);
	void	(*disconnect)(struct db_mysql_t *self);
	void	(*dispose)(struct db_mysql_t *);
	int	(*affected_rows)(struct db_mysql_t *);
	void	(*print_query)(struct db_mysql_t *, const int yn);

	unsigned long long (*insert_id)(struct db_mysql_t *self);
};

struct db_mysql_t * new_dbmysql (void);

#endif
