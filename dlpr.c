/*
 *	dlpr.c
 *
 *	Copyright (c) 2004, Jiann-Ching Liu
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include "xtimer.h"

#ifndef ENABLE_MYSQL_SUPPORT
#define ENABLE_MYSQL_SUPPORT    1
#endif

#if ENABLE_MYSQL_SUPPORT == 1
#include "db_mysql.h"
#include "x_object.h"
#include "sys_conf.h"
#include "base64.h"
#endif

struct lpr_cmd {
	char	cmdcode;
	char	buffer[1024];
};


#define TIMEOUT		10000

static short		disconnect = 0;
static char		*my_hostname = NULL;
static char		*my_username = NULL;
static char		*my_jobname  = NULL;
static int		my_seqno = 0;
static struct xtimer_t	*timer = NULL;
static int		my_uid = 0;
static pid_t		my_pid = 0;
static char		*printer_name = NULL;
// static char		*username = NULL;

// #define DEFAULT_USERNAME	"prtserv"

#if ENABLE_MYSQL_SUPPORT == 1

extern int		epson_page (const char *);

static struct sysconf_t			*sysconfig = NULL;
static struct db_mysql_t		*mydb  = NULL;
static struct x_object_interface_t	*obji  = NULL;

static char				*lpr_printer_ip = NULL;
static char				*lpr_printer_q  = NULL;
static int				my_printer_id = 0;
static int				my_queue_id = 0;

static char				user_acct[20] = "";
static int				pj_sn = 0;
static int				ntdpp = 0;
// static int				user_quota = 0;
static int				spo_sn = 0;
static int				pq_sn  = 0;
static char				*p_banner = NULL;
static char				*client_ip = NULL;
static int				num_pages = 0;
static int				max_free_per_day = 100;
#endif

static int try_read (const int fd, void *buffer,
					const int len, const int msec) {
	fd_set		rset;
	struct timeval	timeout;
	int		maxfd;
	int		ln;

	/* set mutiplex I/O check bit */
	maxfd = fd + 1;
	FD_ZERO(&rset);
	FD_SET(fd, &rset);

	timeout.tv_sec  = msec / 1000;
	timeout.tv_usec = (msec % 1000) * 1000;

	if (select (maxfd, &rset, NULL, NULL, &timeout) < 0) {
		switch (errno) {
		case EINTR:
			return 0;
			break;
		default:
			perror ("select");
			return -1;
		}
	}

	if (FD_ISSET(fd, &rset)) {
		if ((ln = read(fd, buffer, len)) <= 0) disconnect = 1;
		return ln;
	} else {
		return 0;   /* no data-in */
	} 
}

static int tcp_connection (const char *host, const int port) {
	int			sockfd = -1;
	struct sockaddr_in	server;
	struct hostent		*hp;

	if ((hp = gethostbyname (host)) == 0) {
		fprintf (stderr, "%s:%d (%d) %s\n",
				__FILE__, __LINE__, errno,
				strerror (errno));
		return -1;
	}

	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf (stderr, "%s:%d (%d) %s\n",
				__FILE__, __LINE__, errno,
				strerror (errno));
		return -1;
	}

	bcopy ((char*) hp->h_addr, (char*) &server.sin_addr, hp->h_length);
	server.sin_family	= AF_INET;
	server.sin_port		= htons (port);

	seteuid (0);
	if (geteuid () == 0) {
		struct sockaddr_in	myaddr;
		int			lport;

		for (lport = 721; lport <= 731; lport++) {
			myaddr.sin_family	= AF_INET;
			myaddr.sin_addr.s_addr	= INADDR_ANY;
			myaddr.sin_port		= htons (lport);

			if (bind (sockfd, (struct sockaddr*) &myaddr,
							sizeof myaddr) < 0) {
				// fprintf (stderr, "%s:%d (%d) %s\n",
				//	__FILE__, __LINE__, errno,
				//	strerror (errno));
			} else {
				fprintf (stderr, "bind to %d\n", lport);
				break;
			}
		}
	}
	seteuid (my_uid);

	// fprintf (stderr, "My-Uid=%d,%d\n", getuid (), geteuid ());

	if (connect (sockfd, (struct sockaddr*) &server, sizeof server) < 0) {
		fprintf (stderr, "%s:%d (%d) %s\n",
				__FILE__, __LINE__, errno,
				strerror (errno));
		close (sockfd);
		return -1;
	}
	
	return sockfd;
}

static int do_lpr_cancel (const char *lpr_host, const char *lpr_queue) {
	int		sockfd;
	struct lpr_cmd	cmd;
	char		buffer[65536];
	int		rc = 0;

	if ((sockfd = tcp_connection (lpr_host, 515)) < 0) return -1;

	cmd.cmdcode = 5;
	snprintf (cmd.buffer, sizeof cmd.buffer - 1,
				"%s root\n", lpr_queue);
	write (sockfd, &cmd, strlen (cmd.buffer) + 1);

	if (try_read (sockfd, buffer, sizeof buffer, TIMEOUT) > 0) {
		fprintf (stderr, "%s", buffer);
		rc = 1;
	} else {
		fprintf (stderr, "Disconnect\n");
	}

	close (sockfd);

	return rc;
}

static int do_lpr_job (const char *lpr_host, const char *lpr_queue) {
	int		sockfd;
	struct lpr_cmd	cmd;
	char		buffer[65536];
	int		rc = 0;
	const char	*no_entries = "no entries";
#if ENABLE_MYSQL_SUPPORT == 1
	char		b64buf[65536];
#endif

	if ((sockfd = tcp_connection (lpr_host, 515)) < 0) return -1;

	cmd.cmdcode = 3;
	snprintf (cmd.buffer, sizeof cmd.buffer - 1, "%s\n", lpr_queue);
	write (sockfd, &cmd, strlen (cmd.buffer) + 1);

	if (try_read (sockfd, buffer, sizeof buffer, TIMEOUT) > 0) {
		fprintf (stderr, "%s", buffer);
#if ENABLE_MYSQL_SUPPORT == 1
		base64_encode (buffer, strlen (buffer), b64buf, sizeof b64buf);

		mydb->query (mydb,
			"REPLACE INTO lpstate (pid,message) VALUES (%d,'%s')",
			my_printer_id, b64buf
		);
#endif
		if (strncmp (no_entries, buffer, strlen (no_entries)) == 0) {
			rc = 2;
		} else {
			rc = 1;
		}
	}

	close (sockfd);

	return rc;
}

static int do_lpr (const char *lpr_host, const char *lpr_queue,
			const int fd, const char *fname, const int fsize) {
	int		sockfd, len, tlen;
	struct lpr_cmd	cmd;
	char		buffer[65536];
	char		ctrlbuf[4096];
	int		i, j;

	if ((sockfd = tcp_connection (lpr_host, 515)) < 0) return -1;

	do {

		fprintf (stderr, "Sending printer job to [%s]\n", lpr_queue);

		cmd.cmdcode = 2;	// Send a printer job
		snprintf (cmd.buffer, sizeof cmd.buffer - 1, "%s\n", lpr_queue);
		cmd.buffer[sizeof cmd.buffer - 1] = '\0';

		write (sockfd, &cmd, strlen (cmd.buffer) + 1);

		if (try_read (sockfd, buffer, sizeof buffer, TIMEOUT) > 0) {
			if (buffer[0] == '\1') {
				fprintf (stderr, "No such queue [%s]\n",
						lpr_queue);
				break;
			} else if (buffer[0] != '\0') {
				fprintf (stderr, "No such queue [%s](%s)\n",
						lpr_queue, buffer);
				break;
			}
		}
		if (disconnect) break;

		// ------------------------------------------------------

		cmd.cmdcode = 2;	// Send Control file

		len = sprintf (ctrlbuf,
			"H%s\n"
			"P%s\n"
			"J%s\n"
			"ldfA%03d%s\n"
			"UdfA%03d%s\n"
			"N%s\n",
			my_hostname, my_username, my_jobname,
			my_seqno, my_hostname,
			my_seqno, my_hostname,
			my_jobname
		);

		fprintf (stderr, "%s", ctrlbuf);

		sprintf (cmd.buffer, "%d cfA%03d%s\n",
				len, my_seqno, my_hostname);

		timer->start (timer);

		i = strlen (cmd.buffer) + 1;
		fprintf (stderr, "Sending control file ... (%d:", i);
		i = write (sockfd, &cmd, i);
		fprintf (stderr, "%d) ... ", i);

		if (try_read (sockfd, buffer, sizeof buffer, TIMEOUT) > 0) {
			if (buffer[0] != '\0') {
				fprintf (stderr, "Return: %s\n", buffer);
				break;
			}
			try_read (sockfd, buffer, sizeof buffer, 1);
		}
		if (disconnect) break;

		// ------------------------------------------------------
		
		write (sockfd, ctrlbuf, len);
		write (sockfd, "", 1);
		fprintf (stderr, "%d msec. (%d)\n",
					timer->elapsed (timer), len);
		fprintf (stderr, "Wait for response ... ");

		timer->start (timer);
		if (try_read (sockfd, buffer, sizeof buffer, 500) > 0) {
			if (buffer[0] != '\0') {
				fprintf (stderr, "Return: %s\n", buffer);
				break;
			}
			fprintf (stderr, "OK 3\n");
		}
		if (disconnect) break;
		fprintf (stderr, "%d msec\n", timer->elapsed (timer));

		// ------------------------------------------------------

		cmd.cmdcode = 3;	// Send data file
		sprintf (cmd.buffer, "%d dfA%03d%s\n",
				fsize == 0 ? 2147483647 : fsize,
				my_seqno, my_hostname);
		fprintf (stderr, "%s", cmd.buffer);
		write (sockfd, &cmd, strlen (cmd.buffer) + 1);

		fprintf (stderr, "Sending data file ... ");

		timer->start (timer);
		if (try_read (sockfd, buffer, sizeof buffer, 1000) > 0) {
			if (buffer[0] == '\1') {
				fprintf (stderr, "Error\n");
				break;
			} else if (buffer[0] != '\0') {
				fprintf (stderr, "Return: 0x%02x [%s]\n",
						buffer[0], buffer);
				break;
			}
			// fprintf (stderr, "OK 4\n");
		}
		if (disconnect) break;
		fprintf (stderr, " %d msec\n", timer->elapsed (timer));

		// ------------------------------------------------------

		timer->start (timer);
		tlen = 0;

		while ((len = read (fd, buffer, sizeof buffer)) > 0) {
			for (i = j = 0; len > 0; len -= j, i += j) {
				if ((j = write (sockfd, &buffer[i], len)) <= 0){
					disconnect = 1;
					break;
				}
				tlen += j;

				if (try_read (sockfd, ctrlbuf,
						sizeof ctrlbuf, 5) > 0) {
					if (ctrlbuf[0] != '\0') {
						fprintf (stderr,
							"Return: [%s]\n",
							ctrlbuf);
					}
				}
				if (disconnect) break;
			}

			if (disconnect) break;
		}

		write (sockfd, "", 1);

		if (try_read (sockfd, ctrlbuf, sizeof ctrlbuf, TIMEOUT) > 0) {
			if (ctrlbuf[0] != '\0') {
				fprintf (stderr, "Return: [%s]\n",
							ctrlbuf);
			}
			if (disconnect) break;
		}

		fprintf (stderr, "(%d) %.2f KB/s (%s)\n",
			tlen,
			(double ) tlen / 1024. * 1000. /
			timer->elapsed (timer),
			disconnect ? "disconnect" : "ok"
		);
	} while (0);

	close (sockfd);

	return 0;
}

#if ENABLE_MYSQL_SUPPORT == 1
static int request_or_free_printer (const short req) {
	char	**result;

	if (req) {
		mydb->query (mydb,
			"UPDATE printer SET process_id=%d WHERE pname='%s' "
			"AND pstate=1 AND process_id=0",
			my_pid, printer_name
		);

		if (mydb->affected_rows (mydb) == 0) {
			fprintf (stderr, "No printer [%s] available !\n",
					printer_name);
			return 0;
		}


		mydb->query (mydb,
			"SELECT pid,qid,ipaddr,qname "
			" FROM printer WHERE pname='%s' "
			"AND process_id=%d",
			printer_name, my_pid
		);

		if ((result = mydb->fetch (mydb)) != NULL) {
			my_printer_id  = atoi   (result[0]);
			my_queue_id    = atoi   (result[1]);
			lpr_printer_ip = strdup (result[2]);
			lpr_printer_q  = strdup (result[3]);
		} else {
			request_or_free_printer (0);
			return 0;
		}

		return 1;
	} else {
		/*
		mydb->query (mydb,
			"DELETE FROM pj2print WHERE pid=%d AND account=NULL "
			"AND process_id=%d AND sn=%d",
			my_printer_id, my_pid, pj_sn);
		*/

		mydb->query (mydb,
			"UPDATE printer SET process_id=0 WHERE pname='%s' "
			"AND pstate=1 AND process_id=%d",
			printer_name, my_pid
		);
	}

	return 1;
}
#endif

int main (int argc, char *argv[]) {
	int			c, fd;
	int			errflag    = 0;
	char			*lpr_host  = "140.115.1.149";
	char			*lpr_queue = "no_name";
	char			*cfgfile = NULL;
	struct stat		stbuf;
	int			verbose = 0;
	short			remove_flag = 0;
	short			query_flag = 0;
#if ENABLE_MYSQL_SUPPORT == 1
	int			i, rc;
	int			user_opt = 0;
	char			**result;
	struct x_object_t	*xobj;
	char			*file_to_print = NULL;
	int			pcnt = 0;
	pid_t			wake_up = 0;
#endif
	// struct passwd	*pwd;

	// fprintf (stderr, "My-Uid=%d,%d\n", getuid (), geteuid ());
	my_uid = getuid ();
	seteuid (my_uid);
	// fprintf (stderr, "My-Uid=%d,%d\n", getuid (), geteuid ());

#if ENABLE_MYSQL_SUPPORT == 1
	fprintf (stderr, "LPR Print (Direct-LPR) v"
			VERSION
			", Copyright (c) 2004, Jiann-Ching Liu\n\n");
#else
	fprintf (stderr, "Direct-LPR v"
			VERSION
			", Copyright (c) 2004, Jiann-Ching Liu\n\n");
#endif
	while ((c = getopt (argc, argv, "f:h:kp:Qq:vw")) != EOF) {
		switch (c) {
		case 'f':
			cfgfile = optarg;
			break;
		case 'h':
			lpr_host = optarg;
			break;
		case 'q':
			lpr_queue = optarg;
			break;
		case 'k':
			remove_flag = 1;
			break;
		case 'Q':
			query_flag = 1;
			break;
		case 'p':
			printer_name = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'w':
#if ENABLE_MYSQL_SUPPORT == 1
			wake_up = getppid ();
#endif
			break;
		default:
			errflag++;
			break;
		}
	}

#if ENABLE_MYSQL_SUPPORT != 1
	if ((! remove_flag && ! query_flag && (optind >= argc)) || errflag) {
		fprintf (stderr, "dlpr [-h host][-q queue] file ...\n\n");
		return 1;
	}
#endif

	{
		char		hname[128];
		char		*ptr;
		struct passwd	*pw;

		my_seqno = getpid () % 1000;

		if (gethostname (hname, sizeof hname - 1) < 0) {
			my_hostname = "unknow";
		} else {
			if ((ptr = strchr (hname, '.')) != NULL) *ptr = '\0';

			my_hostname = strdup (hname);
		}

		if ((pw = getpwuid (my_uid)) == NULL) {
			my_username = "noname";
		} else {
			my_username = strdup (pw->pw_name);
		}

		my_jobname  = "testing only";
	}

	timer = new_xtimer ();

#if ENABLE_MYSQL_SUPPORT == 1
	if (cfgfile == NULL) {
		char	*cfg_tailer = ".conf";

		if ((cfgfile = alloca (strlen (argv[0]) +
				sizeof cfg_tailer + 1)) == NULL) {
			perror ("alloca");
			return 0;
		} else {
			sprintf (cfgfile, "%s%s", argv[0], cfg_tailer);
		}
	}

	if ((sysconfig = initial_sysconf_module (
			cfgfile, "cmdline-opt", user_opt)) == NULL) {
		return 0;
	}

	if ((i = sysconfig->getint ("max-free-pages-per-day")) > 0) {
		max_free_per_day = i;
	}

	/*
	if (username == NULL) {
		if ((username = sysconfig->getstr ("run-as")) == NULL) {
			username = DEFAULT_USERNAME;
		}
	}
	*/
#else
	// if (username == NULL) username = DEFAULT_USERNAME;
#endif
	/*
	if ((pwd = getpwnam (username)) == NULL) {
		fprintf (stderr, "%s: unknow user\n", username);
		exit (1);
	}
	*/

	// setregid (pwd->pw_gid, pwd->pw_gid);
	// setreuid (pwd->pw_uid, pwd->pw_uid);
	// setregid (pwd->pw_gid, pwd->pw_gid);

	my_pid = getpid ();

#if ENABLE_MYSQL_SUPPORT == 1
	if (printer_name == NULL) {
		fprintf (stderr, "printer_name not defined\n");
		return 0;
	}

	obji = init_x_object_interface ();

	if ((mydb = new_dbmysql ()) == NULL) {
		perror ("new_dbmysql");
		return 0;
	} else {
		mydb->verbose (mydb, 1);

		if (mydb->connect (mydb,
			sysconfig->getstr ("mysql-server"),
			sysconfig->getstr ("mysql-user"),
			sysconfig->getstr ("mysql-password"),
			sysconfig->getstr ("mysql-database"))) {
		} else {
			mydb->perror (mydb, "mysql");
			return 0;
		}

		if (! request_or_free_printer (1)) {
			mydb->dispose (mydb);
			return 0;
		}
	}

	if (query_flag) {
		do_lpr_job (lpr_printer_ip, lpr_printer_q);
		request_or_free_printer (0);
		mydb->dispose (mydb);
		return 0;
	}
	
	if (remove_flag) {
		do_lpr_cancel (lpr_printer_ip, lpr_printer_q);
		do_lpr_job (lpr_printer_ip, lpr_printer_q);
		request_or_free_printer (0);
		mydb->dispose (mydb);
		return 0;
	}

	if (do_lpr_job (lpr_printer_ip, lpr_printer_q) < 2) {
		request_or_free_printer (0);
		mydb->dispose (mydb);
		return 0;
	}
#endif

#if ENABLE_MYSQL_SUPPORT == 1
	for (i = rc = 0; i < 3; i++) {
		mydb->query (mydb,
			"SELECT * FROM spooler "
			"LEFT JOIN pqjob USING (sn) "
			"LEFT JOIN pqueue USING (qid) "
			"WHERE pqjob.qid = %d AND spooler.spst = 0 "
			"LIMIT 1",
			my_queue_id
		);

		if ((xobj = mydb->fetch_array (mydb)) != NULL) {
			pq_sn  = atoi (obji->get (xobj, "sn"));
			spo_sn = atoi (obji->get (xobj, "serial_no"));

			mydb->query (mydb,
				"UPDATE spooler SET spst=1,pid=%d "
				"WHERE spst=0 AND "
				"serial_no = %d", my_printer_id, spo_sn
			);

			if ((rc = mydb->affected_rows (mydb)) > 0) break;
		} else {
			break;
		}
	}

	if (rc == 0) {
		fprintf (stderr, "No printer job in spooler\n");
		request_or_free_printer (0);
		mydb->dispose (mydb);
		exit (0);
	}

	strncpy (user_acct, obji->get (xobj, "account"), sizeof user_acct);
	pj_sn = atoi (obji->get (xobj, "sn"));
	ntdpp = atoi (obji->get (xobj, "ntdpp"));

	file_to_print   = strdup (obji->get (xobj, "q_file"));
	client_ip       = strdup (obji->get (xobj, "ip"));
	p_banner        = strdup (obji->get (xobj, "banner"));
	num_pages	= epson_page (file_to_print);

	fprintf (stderr, "[%s][%s][%s][%s][%s] RC=%d,Pages=%d\n",
		obji->get (xobj, "serial_no"),
		obji->get (xobj, "sn"),
		obji->get (xobj, "account"),
		obji->get (xobj, "q_file"),
		obji->get (xobj, "ntdpp"),
		rc, num_pages
	);

	if (num_pages < 0) num_pages = 0;

	mydb->query (mydb,
		"SELECT pcnt FROM page_of_the_day WHERE account='%s' "
		"AND pdate = NOW()",
		user_acct
	);

	if ((result = mydb->fetch (mydb)) != NULL) {
		pcnt = atoi (result[0]);
	} else {
		mydb->query (mydb,
			"SELECT @pdate := pdate FROM page_of_the_day "
			"WHERE account='%s'",
			user_acct
		);

		if ((result = mydb->fetch (mydb)) != NULL) {
			mydb->query (mydb,
				"UPDATE page_of_the_day SET pdate=NOW(),pcnt=0 "
				"WHERE account='%s' AND pdate=@pdate",
				user_acct
			);
		} else {
			mydb->query (mydb,
				"INSERT INTO page_of_the_day ("
				"account,pdate,pcnt) "
				"VALUES ('%s',NOW(),0)",
				user_acct
			);
		}

		pcnt = 0;
	}

	/*
	mydb->query (mydb,
		"SELECT fuod FROM pquota WHERE account = '%s' AND "
		"fudate = NOW()",
		user_acct
	);
	*/

	if (num_pages + pcnt > max_free_per_day) {
		num_pages = -3;
	} else if ((fd = open (file_to_print, O_RDONLY)) >= 0) {
		if (fstat (fd, &stbuf) == 0) {
			if (do_lpr (lpr_printer_ip, lpr_printer_q, fd,
					file_to_print, stbuf.st_size) < 0) {
				num_pages = -1;
			}
		}
		close (fd);
	} else {
		num_pages = -2;
	}

	unlink (file_to_print);


	// wait until

	if (num_pages >= 0) {
		mydb->query (mydb,
			"UPDATE page_of_the_day SET pcnt=pcnt+%d "
			"WHERE account='%s'",
			num_pages, user_acct
		);

		mydb->query (mydb,
			"INSERT INTO pquota (account) VALUES ('%s')",
			user_acct
		);

		mydb->query (mydb,
			"UPDATE pquota SET fuse=fuse+%d,lastuse=NOW() "
			"WHERE account='%s'",
			num_pages, user_acct
		);

		mydb->query (mydb,
			"INSERT INTO uselog (account,qid,pprint,unituse,"
			"logdate,ip,banner) "
			"VALUES ('%s',%d,%d,%d,NOW(),'%s','%s')",
			user_acct, my_queue_id,
			num_pages, 0, client_ip, p_banner
		);

		mydb->query (mydb,
			"UPDATE printer SET prncnt=prncnt+%d,usecnt=usecnt+1,"
			"lastuse=NOW() WHERE pid=%d",
			num_pages, my_printer_id
		);
	}
	
	mydb->query (mydb,
		"DELETE FROM spooler WHERE spst=1 AND serial_no = %d", spo_sn
	);

	mydb->query (mydb, "DELETE FROM pqjob WHERE sn = %d", pq_sn);

	sleep (5);

	while (do_lpr_job (lpr_printer_ip, lpr_printer_q) != 2) {
		sleep (5);
	}

	mydb->query (mydb,
		"UPDATE page_of_the_day SET locked=0 "
		"WHERE account='%s'", user_acct
	);

	request_or_free_printer (0);

	if (wake_up > 0) kill (wake_up, SIGUSR1);
#else
	if (query_flag) {
		do_lpr_job (lpr_host, lpr_queue);
		return 0;
	}

	if (remove_flag) {
		do_lpr_cancel (lpr_host, lpr_queue);
		return 0;
	}

	switch (do_lpr_job (lpr_host, lpr_queue)) {
	case 1:
	case 2:
		if ((optind + 1 == argc) && (strcmp (argv[optind], "-") == 0)) {
			fd = STDIN_FILENO;
			do_lpr (lpr_host, lpr_queue, fd, "(stdin)", 0);
		} else {
			for (; optind < argc; optind++) {
				if ((fd = open (argv[optind], O_RDONLY)) >= 0) {
					if (fstat (fd, &stbuf) == 0) {
						if (do_lpr (lpr_host, lpr_queue,
							fd, argv[optind],
							stbuf.st_size) < 0) {
							optind = argc;
						}
					}
					close (fd);
				}
			}
		}
		break;
		// do_lpr_cancel (lpr_host, lpr_queue);
	default:
		break;
	}
#endif

#if ENABLE_MYSQL_SUPPORT == 1
	mydb->dispose (mydb);
#endif
	timer->dispose (timer);

	return 0;
}

