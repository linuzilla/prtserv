/*
 *	pdfwrite.c
 *
 *	Copyright (c) 2004, Jiann-Ching Liu
 */


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/errno.h>

#include "db_mysql.h"
#include "x_object.h"
#include "sys_conf.h"
#include "xtimer.h"

#define	MAX(x,y)	((x) > (y) ? (x) : (y))

// int            read_status_job(int fd);

static char	*progname;
static int	debug_flag = 0;
static int	verbose = 0;

static int	number_of_pages = 0;
static int	get_final_result = 0;
static char	*logfile = NULL;
static int	my_pid = 0;
static char	*hp_printer_ip = "140.115.1.149";


struct sysconf_t		*sysconfig = NULL;
struct db_mysql_t		*mydb  = NULL;
struct x_object_interface_t	*obji  = NULL;

static char			*printer_name = "pdf";
static int			my_queue_id = 0;
static char			user_acct[20] = "";
static int			pj_sn = 0;
static int			ntdpp = 0;
static int			precnt = 0;
static int			spo_sn = 0;
static int			pq_sn  = 0;
static char			*p_banner = NULL;
static char			*client_ip = NULL;

static void usage (void) {
	fprintf (stderr,
		"usage: %s [-dv][-c cfgfile]"
		"[-f page#][-t page#][-h hostname] file\n",
		progname
	); 
}

static void interrupt (int signo) {
	signal (signo, interrupt);

	switch (signo) {
	case SIGALRM:
		if (verbose > 2) fprintf (stderr, "[Time Up]\n");
		if (get_final_result == 0) get_final_result = 3;
		break;
	case SIGHUP:
		break;
	case SIGINT:
	case SIGTERM:
		if (get_final_result == 0) get_final_result = 2;
		// exit(1);
	default:
		break;
	}
}

int main (int argc, char *argv[]) {
	int		c, errflag = 0;
	char		*ptr;
	int		i;
	char		*cfgfile = NULL;
	char		*file_to_print = NULL;
	int			user_opt = 0;
	char			**result;
	struct x_object_t	*xobj;
	// struct x_object_t	*list;
	struct xtimer_t		*timer;
	pid_t			wake_up = 0;

	progname = (ptr = strrchr (argv[0], '/')) == NULL ? argv[0] : ++ptr;

	while ((c = getopt(argc, argv,  "c:dh:l:p:vw")) != EOF) {
		switch (c) {
		case 'c':
			cfgfile = optarg;
			break;
		case 'l':
			logfile = optarg;
			break;

		case 'h':
			hp_printer_ip = optarg;
			break;

		case 'd':
			debug_flag++;
			break;

		case 'v':
			verbose++;
			break;

		case 'p':
			printer_name = optarg;
			break;
		case 'w':
			wake_up = getppid ();
			break;
/*
		case 'p':
			port = atoi(optarg);
			break;
*/
		default:
			errflag++;
			break;
		}
	}

	if (errflag) {
		usage ();
		exit (1);
	}

	my_pid = getpid ();


	timer = new_xtimer ();

	if (cfgfile == NULL) {
		char    *cfg_tailer = ".conf";

		if ((cfgfile = alloca (strlen (argv[0]) +
					sizeof cfg_tailer + 1)) == NULL) {
			perror ("alloca");
			return 0;
		} else {
			sprintf (cfgfile, "%s%s", argv[0], cfg_tailer);
		}       
	}

	if ((sysconfig = initial_sysconf_module (
			cfgfile, "cmdline-opt", user_opt)) != NULL) {
		if (verbose == 0) {
			if ((i = sysconfig->getint ("verbose")) > 0) {
				verbose = i;
			};
		}
		if (debug_flag == 0) {
			if ((i = sysconfig->getint ("debug")) > 0) {
				debug_flag = i;
			};
		}
	} else {
		exit (0);
	}


	obji = init_x_object_interface ();
	// list = obji->newobj ();
	// obji->empty (list);

	if ((mydb = new_dbmysql ()) == NULL) {
		perror ("new_dbmysql");
		return 0;
	} else {
		int	rc = 0;

		// mydb->print_query (mydb, 1);

		if (mydb->connect (mydb,
				sysconfig->getstr ("mysql-server"),
				sysconfig->getstr ("mysql-user"),
				sysconfig->getstr ("mysql-password"),
				sysconfig->getstr ("mysql-database"))) {
		} else {
			mydb->perror (mydb, "mysql");
			return 0;
		}

		mydb->query (mydb,
			"SELECT qid FROM printer WHERE pname='%s'",
			printer_name
		);

		if ((result = mydb->fetch (mydb)) != NULL) {
			my_queue_id   = atoi (result[0]);
		} else {
			mydb->dispose (mydb);
			return 0;
		}

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
					"UPDATE spooler SET spst=1 "
					"WHERE spst=0 AND "
					"serial_no = %d", spo_sn
				);

				if ((rc = mydb->affected_rows (mydb)) > 0) {
					break;
				}
			} else {
				break;
			}
		}

		if (rc == 0) {
			fprintf (stderr, "No printer job in spooler\n");
			mydb->dispose (mydb);
			exit (1);
		}

		strncpy (user_acct, obji->get (xobj, "account"),
				sizeof user_acct);
		pj_sn  = atoi (obji->get (xobj, "sn"));
		ntdpp  = atoi (obji->get (xobj, "ntdpp"));
		number_of_pages = precnt = atoi (obji->get (xobj, "precnt"));

		file_to_print	= strdup (obji->get (xobj, "q_file"));
		client_ip	= strdup (obji->get (xobj, "ip"));
		p_banner	= strdup (obji->get (xobj, "banner"));


		fprintf (stderr, "[%s][%s][%s][%s][%s] RC=%d\n",
				obji->get (xobj, "serial_no"),
				obji->get (xobj, "sn"),
				obji->get (xobj, "account"),
				obji->get (xobj, "q_file"),
				obji->get (xobj, "ntdpp"),
				rc
		);

		/*
		mydb->query (mydb,
			"UPDATE spooler SET spst=0 "
			"WHERE spst=1 AND "
			"serial_no = %s",
			obji->get (xobj, "serial_no")
		);
		*/
		// if ((result = mydb->fetch (mydb)) != NULL) {
	}

	signal (SIGINT,  interrupt);
	signal (SIGHUP,  interrupt);
	signal (SIGTERM, interrupt);
	signal (SIGALRM, SIG_IGN);
	signal (SIGCHLD, SIG_IGN); 

	if (file_to_print != NULL) {
		char		cmdstr[1024];
		char		pdf_file[1024];
		char		basedir[1024];
		char		*ptr;
		struct stat	stbuf;

		if (access (file_to_print, R_OK) != 0) {
			perror (file_to_print);

			mydb->query (mydb,
				"DELETE FROM pqjob WHERE sn = %d", pq_sn
			);

			mydb->query (mydb,
				"DELETE FROM spooler WHERE serial_no = %d",
				spo_sn
			);

			mydb->dispose (mydb);
			exit (1);
		}

		strcpy (basedir, file_to_print);
		if ((ptr = strrchr (basedir, '/')) != NULL) {
			*ptr = '\0';
		}

		sprintf (pdf_file, "%s/PDF/pdf-%s-%d.pdf",
					basedir, user_acct, pq_sn);

		sprintf (cmdstr, "/usr/local/bin/gs -dNOPAUSE -q "
				"-dBATCH -dSAFER -sDEVICE=pdfwrite "
				"-sOutputFile=%s %s > /dev/null 2>&1",
				pdf_file, file_to_print);
		system (cmdstr);
			
		if (stat (pdf_file, &stbuf) == 0) {
			mydb->query (mydb,
				"INSERT INTO pdfjob "
				"(account,banner,pdf_file,fsize,gdate) "
				"VALUES ('%s','%s','%s',%d,NOW())",
				user_acct, p_banner, pdf_file, stbuf.st_size
			);
		}
	}

	mydb->query (mydb,
		"INSERT INTO uselog (account,qid,pprint,unituse,"
		"precnt,logdate,ip,banner) "
		"VALUES ('%s',%d,%d,%d,%d,NOW(),'%s','%s')",
		user_acct, my_queue_id,
		number_of_pages, number_of_pages * ntdpp,
		precnt, client_ip, p_banner
	);

	mydb->query (mydb,
		"DELETE FROM pqjob WHERE sn = %d", pq_sn
	);

	mydb->query (mydb,
		"DELETE FROM spooler WHERE serial_no = %d", spo_sn
	);


	if (file_to_print != NULL) unlink (file_to_print);


	mydb->dispose (mydb);

	timer->dispose (timer);

	if (wake_up > 0) kill (wake_up, SIGUSR1);

	return 0;
}
