/*
 *	sendps.c
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
#include <regex.h>

#ifndef ENABLE_MYSQL_SUPPORT
#define ENABLE_MYSQL_SUPPORT	1
#endif

#if ENABLE_MYSQL_SUPPORT == 1
#  define ENABLE_SNMP_SUPPORT	1
#else
#  define ENABLE_SNMP_SUPPORT	0
#endif

#if ENABLE_MYSQL_SUPPORT == 1
#include "db_mysql.h"
#include "x_object.h"
#include "sys_conf.h"
#include "xtimer.h"
#endif

#if ENABLE_SNMP_SUPPORT == 1
#include "snmp_lib.h"
#endif

#define	MAX(x,y)	((x) > (y) ? (x) : (y))

#define MIBs_GET_PAGECNT	".1.3.6.1.2.1.43.10.2.1.4.1.1"
#define MIBs_GET_PANNEL		".1.3.6.1.2.1.43.16.5.1.2.1.1"

const char *pjl_uel_spjl          = "\x1B%%-12345X";
const char *escape_E              = "\x1B" "E";

// int            read_status_job(int fd);

static FILE	*stdxx = NULL;
static char	*progname;
static int	startpage = 1;
static int	endpage = 0;
static int	debug_flag = 0;
static int	verbose = 0;
static int	disconnect = 0;

static char	username[20];
static char	jobname[64];
static char	pjob[64];

static int	pjl_state = 0;
static int	my_state = 0;
static int	number_of_pages = 0;
static int	get_final_result = 0;
static int	user_canceled = 0;
static FILE	*logfp = NULL;
static char	*logfile = NULL;
static int	my_pid = 0;
static char	*hp_printer_ip = "140.115.1.149";
static int	lcode = 0;

#if ENABLE_SNMP_SUPPORT == 1
static char	**printer_ready = NULL;
static char	**printer_busy  = NULL;
static int	num_p_ready = 0;
static int	num_p_busy  = 0;
static char	*community = "public";
#endif

#if ENABLE_MYSQL_SUPPORT == 1
struct sysconf_t		*sysconfig = NULL;
struct db_mysql_t		*mydb  = NULL;
struct x_object_interface_t	*obji  = NULL;

static char			*printer_name = "hp9000";
static int			my_printer_id = 0;
static int			my_queue_id = 0;
static char			user_acct[20] = "";
static int			pj_sn = 0;
static int			ntdpp = 0;
static int			user_quota = 0;
static int			spo_sn = 0;
static int			pq_sn  = 0;
static char			*p_banner = NULL;
static char			*client_ip = NULL;
#endif
#if ENABLE_SNMP_SUPPORT == 1
static struct snmp_lib_t	*snmp = NULL;
#endif

struct pjlreg_t {
	regex_t		preg;
	regmatch_t	*pmatch;
	int		n;
};

struct pjl_state_t {
	short		ptype;
	short		ponly;
	short		p_or_f;
	short		pstate;
	short		value;
	struct pjlreg_t	*reg;
	char		*string;
};

static struct pjl_state_t	pjl_table[] = {
	{ 1, 0, 1, 1,  1, NULL, "@PJL USTATUS DEVICE" },
	{ 1, 0, 1, 2,  2, NULL, "@PJL USTATUS JOB" },
	{ 1, 0, 1, 3,  3, NULL, "@PJL INFO STATUS" },
	{ 1, 0, 1, 4,  4, NULL, "@PJL USTATUS PAGE" },
	{ 2, 0, 0, 0,  5, NULL, "%%[" },
	{ 3, 1, 0, 0,  6, NULL, "ID=" },
	{ 3, 0, 2, 0,  7, NULL, "^CODE=([0-9]+)$" },
	{ 3, 0, 2, 0,  8, NULL, "^CODE2=([0-9]+)$" },
	{ 3, 0, 0, 0,  9, NULL, "ID=" },
	{ 3, 4, 2, 0, 10, NULL, "^[0-9]+$" },
	{ 3, 2, 1, 0, 11, NULL, "CANCELED" },
	{ 3, 2, 1, 0, 12, NULL, "END" },
	{ 3, 2, 1, 0, 20, NULL, "START" },
	{ 3, 2, 2, 0, 13, NULL, "^PAGES=([0-9]+)$" },
	{ 3, 2, 1, 0, 14, NULL, "RESULT=OK" },
	{ 3, 0, 1, 0, 50, NULL, "RESULT=USER_CANCELED" },
	{ 3, 2, 2, 0, 15, NULL, "^RESULT=(.*)$" },
	{ 3, 0, 2, 0, 16, NULL, "^DISPLAY=\"(.*)\"$" },
	{ 3, 0, 1, 0, 30, NULL, "ONLINE=TRUE" },
	{ 3, 0, 1, 0, 31, NULL, "ONLINE=FALSE" },
	{ 3, 2, 0, 0, 40, NULL, "NAME=" }
};

static void write_logfile (const char *str) {
	if (logfp == NULL) return;
	fprintf (logfp, "%s\n", str);
}

static void pjl_receieve (char *str) {
	int			i, j, code, len, match;
	struct pjl_state_t	*ptr;
	struct pjlreg_t		*reg;
	int			page_cnt;

	len = sizeof pjl_table / sizeof (struct pjl_state_t);


	for (i = match = 0; i < len; i++) {
		ptr = &pjl_table[i];
		reg = ptr->reg;

		if (ptr->ponly != 0 && ptr->ponly != pjl_state) continue;

		if (ptr->p_or_f == 2) {
			if (regexec (&(reg->preg), str, reg->n,
						reg->pmatch, 0) == 0) {
				match = 1;
			}
		} else if (ptr->p_or_f == 1) {
			if (strcmp (str, ptr->string) == 0) {
				match = 1;
			}
		} else {
			j = strlen (ptr->string);

			if (strncmp (str, ptr->string, j) == 0) {
				match = 1;
			}
		}

		if (! match) continue;

		if (ptr->ptype == 1) pjl_state = ptr->pstate;

		if (ptr->ptype == 1) {
			// fprintf (stdxx,
			// 	"[%s] ST -> %d\n", str, pjl_state);
			// break;
		} else if (ptr->ptype == 2) { // %%
		} else if (ptr->ptype == 3) {
		}

		if (verbose > 2) fprintf (stdxx, "<-[%s]\n", str);

		switch (ptr->value) {
		case  1:	// @PJL USTATUS DEVICE
		case  2:	// @PJL USTATUS JOB
		case  3:	// @PJL INFO STATUS
		case  4:	// @PJL USTATUS PAGE
			// fprintf (stdxx, "(%s) V=%d\n", str, ptr->value);
			break;
		case  5:	// %%[
			write_logfile (str);
			break;
		case  6:	// ID=
		case  9:	// ID=
			// fprintf (stdxx, "(%s) V=%d\n", str, ptr->value);
			break;
		case  7:	// CODE=
		case  8:	// CODE2=
			lcode = (str[reg->pmatch[1].rm_so] - '0') * 10 +
				(str[reg->pmatch[1].rm_so + 1] - '0');
			str[reg->pmatch[1].rm_eo] = '\0';
			code = atoi (&str[reg->pmatch[1].rm_so]);

			switch (lcode) {
			case 10:	// information
			case 11:	// background paper-loading
			case 12:	// background paper-tray status
			case 15:	// output-bin status
			case 20:	// PJL parse errors
			case 25:	// PJL parse warnings
			case 30:	// Auto-continuable conditions
			case 32:	// PJL-file system errors
			case 35:	// Possible operator
					//	intervention conditions
			case 40:	// Operator intervention conditions
			case 41:	// Foreground paper-loading
				break;
			case 42:	// Paper Jam
			case 44:	// HP LaserJet 4000/5000 jam messages
				fprintf (stdxx, "Paper Jam (Code=%d)\n", code);
				break;
			case 43:	// Optional (external)
					//  paper-handling-device
				break;
			case 50:	// Hardware errors
			case 55:	// Personality errors
				break;
			default:
				break;
			}
			// code = atoi (&str[reg->pmatch[1].rm_so]);
		case 10:	// ^[0-9]+$
			if (my_state >= 3) {
				page_cnt = atoi (str);
				fprintf (stdxx,
					"[[Page count=%d]]\n", page_cnt);

				number_of_pages = MAX(page_cnt,number_of_pages);
			}
			break;
		case 11:	// CANCELED
			fprintf (stdxx, "*** CANCELED Receieved\n");
			break;
		case 12:	// END
			fprintf (stdxx, "*** END Receieved\n");
			break;
		case 20:	// START
			fprintf (stdxx, "*** START\n");
			break;
		case 13:	// ^PAGES=([0-9]+)$
			str[reg->pmatch[1].rm_eo] = '\0';
			page_cnt = atoi (&str[reg->pmatch[1].rm_so]);

			if (my_state >= 3) {
				fprintf (stdxx, "[<Page=%d,%d>]\n",
						page_cnt, number_of_pages);
				number_of_pages = MAX(page_cnt,number_of_pages);
				get_final_result = 1;
			}
			break;
		case 14:	// RESULT=OK
			// fprintf (stdxx, "Printer Result=OK\n");
			break;
		case 15:	// ^RESULT=(.*)$
			fprintf (stdxx, "(%s) V=%d\n", str, ptr->value);
			break;
		case 50:	// RESULT=USER_CANCELED
			if (my_state >= 3) user_canceled = 1;
			break;
		case 16:	// ^DISPLAY=\"(.*)\"$
			str[reg->pmatch[1].rm_eo] = '\0';
			fprintf (stdxx, "{%s}\n",
					&str[reg->pmatch[1].rm_so]);
			break;
		case 30:	// ONLINE=TRUE
			// fprintf (stdxx, "Printer Online\n");
			break;
		case 31:	// ONLINE=FALSE
			fprintf (stdxx, "*** Printer Offline\n");
			break;
		case 40:	// NAME=
			fprintf (stdxx, "NAME (%s)\n", str);
			break;
		default:
			fprintf (stdxx, "(%s) V=%d\n", str, ptr->value);
			break;
		}
		break;
	}

	if (! match) {
		if (verbose > 2) {
			fprintf (stdxx, "<-(%s) not match [%d]\n",
							str, pjl_state);
		}
	}
}

static void init_pjl_reg (void) {
	int			i, n, len;
	struct pjlreg_t		*reg;
	struct pjl_state_t	*ptr;


	len = sizeof pjl_table / sizeof (struct pjl_state_t);

	for (i = 0; i < len; i++) {
		if (pjl_table[i].p_or_f == 2) {
			ptr = &pjl_table[i];

			if ((reg = malloc (sizeof (*reg))) == NULL) {
				perror ("malloc");
				exit (1);
			}

			ptr->reg = reg;

			if (regcomp (&(reg->preg),
					ptr->string,
					REG_EXTENDED|REG_NEWLINE) != 0) {
				fprintf (stdxx, "regcomp: compiling error");
				exit (2);
			}

			reg->n = n = reg->preg.re_nsub + 1;

			if ((reg->pmatch = malloc (
					sizeof (regmatch_t) * n)) == NULL) {
				perror ("malloc");
				exit (1);
			}
		}
	}
}


static void usage (void) {
	fprintf (stdxx,
		"usage: %s [-dv][-c cfgfile]"
		"[-f page#][-t page#][-h hostname] file\n",
		progname
	); 
}

#if ENABLE_SNMP_SUPPORT == 1
static int printer_status (void) {
	char	*ptr;
	int	i, len;

	if (snmp == NULL) return -2;
	if ((ptr = snmp->get_str (snmp, MIBs_GET_PANNEL)) == NULL) return -1;

	len = strlen (ptr);

	if (verbose > 2) {
		fprintf (stdxx, "SNMP: [%s]\n", ptr);
	}

	if (printer_ready != NULL) {
		for (i = 0; i < num_p_ready; i++) {
			if (strncmp (printer_ready[i], &ptr[1], len - 2) == 0) {
				return 0;
			}
		}
	}

	if (printer_busy != NULL) {
		for (i = 0; i < num_p_busy; i++) {
			if (strncmp (printer_busy[i], &ptr[1], len - 2) == 0) {
				return 2;
			}
		}
	}

	return 1;
}
#endif

static int tcp_connect_to (const char *host, const int port) {
	int			sock;
	struct sockaddr_in	server;
	struct hostent		*hp, *gethostbyname();

	if ((sock = socket(AF_INET, SOCK_STREAM, 0))< 0) {
		perror ("opening stream socket");
		exit (1);
	}

	server.sin_family = AF_INET;

	if ((hp = gethostbyname(host)) == 0) {
		fprintf (stdxx, "%s: unknow host\n", host);
		exit (1);
	}

	bcopy ((char*)hp->h_addr, (char*)&server.sin_addr, hp->h_length);
	server.sin_port = htons(port);

	if (connect (sock, (struct sockaddr*)&server, sizeof server) < 0) {
		perror("connecting stream socket");
		exit (1);
	}

	return sock;
}

static int read_without_wait (const int fd, void *buffer,
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


static int read_data (const int  fd, const int msec) {
	int	i, j, len;
	char	buffer[1024];

	if (disconnect) return 0;

	while ((len = read_without_wait (fd, buffer,
					sizeof buffer, msec)) > 0) {
		buffer[len] = '\0';

		for (i = j = 0; i < len; i++) {
			if ((buffer[i] == '\r') || (buffer[i] == '\n')) {
				buffer[i] = '\0';
				if (j < i) pjl_receieve (&buffer[j]);
				j = i + 1;
			} else if (buffer[i] == '\f') {
				buffer[i] = '\0';
				if (j < i) pjl_receieve (&buffer[j]);
				j = i + 1;
			} else if (buffer[i] < ' ') {
				if (verbose >= 2) {
					fprintf (stdxx, "<0x%02x>", buffer[i]);
				}
				buffer[i] = ' ';
			}
		}

		if (j < len) pjl_receieve (&buffer[j]);
	}

	return len;
}

static int write_pjl (const int fd, const char *fmt, ...) {
	char	message[4096];
	va_list	ap;
	int	len, i, j;

	read_data (fd, 10);

	va_start (ap, fmt);
	vsnprintf (message, sizeof message - 3, fmt, ap);
	va_end (ap);

	if (verbose > 2) fprintf (stdxx, "->[%s]\n", message);

	len = strlen (message);
	message[len++] = '\n';

	for (i = 0; len > 0; len -= j, i += j) {
		if ((j = write (fd, &message[i], len)) <= 0) break;
	}

	read_data (fd, 500);

	return 0;
}

static int write_data (const int fd, const char *fmt, ...) {
	char	message[4096];
	va_list	ap;
	int	len, i, j;

	va_start (ap, fmt);
	vsnprintf (message, sizeof message - 1, fmt, ap);
	va_end (ap);

	len = strlen (message);
	i   = 0;

	while (len > 0) {
		if ((j = write (fd, &message[i], len)) <= 0) break;
		len -= j;
		i   += j;
	}

	return 0;
}


static void interrupt (int signo) {
	signal (signo, interrupt);

	switch (signo) {
	case SIGALRM:
		if (verbose > 2) fprintf (stdxx, "[Time Up]\n");
		if (get_final_result == 0) get_final_result = 3;
		break;
	case SIGHUP:
		break;
	case SIGINT:
	case SIGTERM:
		if (get_final_result == 0) get_final_result = 2;
		// fprintf (stdxx, "Killed !!\n");
		// exit(1);
	default:
		break;
	}
}

/*
static void pj_header_cut (char *ptr, const int len, const int clen) {
	int	i, j;

	if (clen <= 0) return;

	for (i = clen, j = 0; i < len; i++, j++) ptr[j] = ptr[i];
}

static int pj_header_fixup (char *buffer, const int len) {
	int		i, j, k, m, n;
	const char	*pjl_ent_language = "@PJL ENTER LANGUAGE";

	j = strlen (pjl_uel_spjl);
	m = strlen (pjl_ent_language);
	k = len;

	for (i = 0; i < k; i++) {
		switch (buffer[i]) {
		case '\x1b':	//	ESC
			if (memcmp (&buffer[i], pjl_uel_spjl, j) == 0) {
				pj_header_cut (&buffer[i], k - i, j);
				k -= j;
				i--;
			}
			break;
		case '@':	// 	PJL
			if (strncmp (&buffer[i], pjl_ent_language, m) == 0) {
				for (n = i + m; n < k; n++) {
					if (buffer[n] == ' ') {
					}
				}
			}
			break;
		}
	}

	//^[%-12345X@PJL JOB
	//@PJL SET HOLD=OFF
	//@PJL SET RESOLUTION = 600
	//@PJL SET BITSPERPIXEL = 2
	//@PJL SET ECONOMODE = OFF
	//@PJL ENTER LANGUAGE = POSTSCRIPT
	//
	return k;
}
*/

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
			fprintf (stdxx, "No printer [%s] available !\n",
					printer_name);
			return 0;
		}

		mydb->query (mydb,
			"SELECT pid,qid,ipaddr,community "
			" FROM printer WHERE pname='%s' "
			"AND process_id=%d",
			printer_name, my_pid
		);

		if ((result = mydb->fetch (mydb)) != NULL) {
			my_printer_id = atoi   (result[0]);
			my_queue_id   = atoi   (result[1]);
			hp_printer_ip = strdup (result[2]);
			community     = strdup (result[3]);
		} else {
			request_or_free_printer (0);
			return 0;
		}

		return 1;
	} else {
		mydb->query (mydb,
			"DELETE FROM pj2print WHERE pid=%d AND account='%s' "
			"AND process_id=%d AND sn=%d",
			my_printer_id, user_acct, my_pid, pj_sn);

		mydb->query (mydb,
			"UPDATE printer SET process_id=0 WHERE pname='%s' "
			"AND pstate=1 AND process_id=%d",
			printer_name, my_pid
		);
	}

	return 1;
}
#endif

static int is_user_canceled (void) {
#if ENABLE_MYSQL_SUPPORT == 1
	char	**result;

	mydb->query (mydb,
		"SELECT * FROM spooler WHERE serial_no = %d AND spst=2", spo_sn
	);

	if ((result = mydb->fetch (mydb)) != NULL) {
		fprintf (stdxx, "*** User cancel (via Web)\n");
		return 1;
	}

	return 0;
#else
	return 0;
#endif
}

int main (int argc, char *argv[]) {
	const int	port = 9100;
	int		c, errflag = 0;
	int		sockfd;
	char		*ptr;
	int		fd = -1;
	int		i, j, len = 0;
	char		buffer[65536];
	int		pgb = 0;
	char		prog_banner[] = "\\|/-";
	char		*cfgfile = NULL;
	char		*file_to_print = NULL;
	short		check_user_canceled = 0;
#if ENABLE_MYSQL_SUPPORT == 1
	int			user_opt = 0;
	// char			**result;
	struct x_object_t	*xobj;
	// struct x_object_t	*list;
	struct xtimer_t		*timer;
#endif
#if ENABLE_SNMP_SUPPORT == 1
	int			enable_snmp = 1;
	short			have_cnt = 0;
	int			counter1 = 0;
	int			counter2 = 0;
#endif

	stdxx = stderr;

	progname = (ptr = strrchr (argv[0], '/')) == NULL ? argv[0] : ++ptr;

	while ((c = getopt(argc, argv,  "c:df:h:l:p:t:v")) != EOF) {
		switch (c) {
		case 'c':
			cfgfile = optarg;
			break;
		case 'l':
			logfile = optarg;
			break;
		case 'f':
			startpage = atoi (optarg);
			break;

		case 't':
			endpage = atoi (optarg);
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
#if ENABLE_MYSQL_SUPPORT == 1
			printer_name = optarg;
#endif
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


#if ENABLE_MYSQL_SUPPORT == 1
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
#if ENABLE_SNMP_SUPPORT == 1
		printer_ready = sysconfig->strlist (
					"printer-ready", &num_p_ready);
		printer_busy  = sysconfig->strlist (
					"printer-busy",  &num_p_busy);
#endif
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

		if (! request_or_free_printer (1)) {
			fprintf (stdxx, "Request printer failed !\n");
			mydb->dispose (mydb);
			exit (1);
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
			fprintf (stdxx, "No printer job in spooler\n");
			request_or_free_printer (0);
			mydb->dispose (mydb);
			exit (1);
		}

		strncpy (user_acct, obji->get (xobj, "account"),
				sizeof user_acct);
		pj_sn = atoi (obji->get (xobj, "sn"));
		ntdpp = atoi (obji->get (xobj, "ntdpp"));

		file_to_print	= strdup (obji->get (xobj, "q_file"));
		client_ip	= strdup (obji->get (xobj, "ip"));
		p_banner	= strdup (obji->get (xobj, "banner"));

		fprintf (stdxx, "[%s][%s][%s][%s][%s] RC=%d\n",
				obji->get (xobj, "serial_no"),
				obji->get (xobj, "sn"),
				obji->get (xobj, "account"),
				obji->get (xobj, "q_file"),
				obji->get (xobj, "ntdpp"),
				rc
		);

		if (debug_flag) {
			char	*ptr;
			char	filepath[512];

			if ((ptr = sysconfig->getstr ("logdir")) != NULL) {
				sprintf (filepath, "%s/%s/%08d-%s.log",
						ptr, printer_name, pj_sn,
						user_acct);

				if ((stdxx = fopen (filepath, "a")) == NULL) {
					stdxx = stderr;
				} else {
					setvbuf (stdxx, NULL, _IONBF, 0);
				}
			}
		}

		if (! mydb->query (mydb, "INSERT INTO pj2print "
				"(pid,account,process_id,sn) VALUES "
				"(%d,'%s',%d,%d)",
				my_printer_id, user_acct, my_pid, pj_sn)) {

			// if (mydb->affected_rows (mydb) == 0) {

			fprintf (stdxx, "User or printer Busy !!\n");
			mydb->query (mydb,
				"UPDATE spooler SET spst=0 "
				"WHERE spst=1 AND serial_no = %d",
				spo_sn
			);
			request_or_free_printer (0);

			mydb->dispose (mydb);
			exit (1);
		}

		if (ntdpp > 0) {
			char	**result;

			// Check if user have enough quota to print
			mydb->query (mydb,
				"SELECT pquota FROM pquota WHERE account='%s'",
				user_acct
			);

			if ((result = mydb->fetch (mydb)) != NULL) {
				user_quota = atoi (result[0]);
			}

			if (user_quota < ntdpp) {
				fprintf (stdxx, "Quota: %d\n", user_quota);

				request_or_free_printer (0);

				mydb->dispose (mydb);
				exit (1);
			}
		}


		/*
		mydb->query (mydb,
			"UPDATE spooler SET spst=0 "
			"WHERE spst=1 AND "
			"serial_no = %s",
			obji->get (xobj, "serial_no")
		);
		*/
		// if ((result = mydb->fetch (mydb)) != NULL) {
		// request_or_free_printer (0);
	}
#endif

	if (endpage > 0 && startpage > endpage) {
		int	tmp;

		tmp       = endpage;
		endpage   = startpage;
		startpage = tmp;
	}

	signal (SIGINT,  interrupt);
	signal (SIGHUP,  interrupt);
	signal (SIGTERM, interrupt);
	signal (SIGALRM, SIG_IGN);
	signal (SIGCHLD, SIG_IGN); 

	{
		struct passwd	*pwptr;
		struct utsname	utsn;
		char		*cp;

		if ((pwptr = getpwuid (getuid ())) != NULL) {
			strcpy (username, pwptr->pw_name);
		} else {
			strcpy (username, "nobody");
		}

		uname (&utsn);

		if ((cp = strchr (utsn.nodename, '.')) != NULL) *cp = '\0';

		sprintf (pjob, "%s%05d", utsn.nodename, getpid ());
		sprintf (jobname, "hpunix-%3d", getpid () % 1000);
	}

	init_pjl_reg ();

	if (file_to_print != NULL) {
		if ((fd = open (file_to_print, O_RDONLY)) < 0) {
			perror (file_to_print);
#if ENABLE_MYSQL_SUPPORT == 1
			request_or_free_printer (0);

			mydb->query (mydb,
				"DELETE FROM pqjob WHERE sn = %d", pq_sn
			);

			mydb->query (mydb,
				"DELETE FROM spooler WHERE serial_no = %d",
				spo_sn
			);

			mydb->dispose (mydb);
#endif
			exit (1);
		}
	} else if (optind >= argc) {
		fd = -1;
	} else if (strcmp (argv[optind], "-") == 0) {
		fd = STDIN_FILENO;
	} else {
		if ((fd = open (argv[optind], O_RDONLY)) < 0) {
			perror (argv[optind]);
			exit (1);
		}
	}

	if (fd >= 0) {
		if ((len = read (fd, buffer, sizeof buffer)) > 0) {
			// len = pj_header_fixup (buffer, len);
		} else {
			return 1;
		}
	}


#  if ENABLE_SNMP_SUPPORT == 1
	if (enable_snmp) {
		if ((snmp = new_snmp_lib ()) == NULL) {
			perror ("new_snmp_lib");
			return 1;
		}

		snmp->set (snmp, hp_printer_ip, 161, community);

		if (printer_status () >= 0) {
			have_cnt = 1;
			counter1 = counter2 =
				snmp->get_int32 (snmp, MIBs_GET_PAGECNT);
			fprintf (stdxx, "[Page count=%d]\n", counter1);
		}
	}
#  endif

	if ((sockfd = tcp_connect_to (hp_printer_ip, port)) < 0) {
		fprintf (stdxx, "Can not connect to %s\n", hp_printer_ip);
		exit (1);
	}

	if (verbose > 2) fprintf (stdxx, "Connect to [%s]\n", hp_printer_ip);

	if (logfile != NULL) logfp = fopen (logfile, "a");

	pjl_state = 0;
	my_state  = 0;
	number_of_pages = 0;
	get_final_result = 0;
	user_canceled = 0;

	if (fd < 0) {
		write_pjl  (sockfd, "@PJL INFO STATUS");
		read_data (sockfd, 5000);
	} else {
		write_data (sockfd, pjl_uel_spjl);
		write_pjl  (sockfd, "@PJL EOJ");
		write_data (sockfd, pjl_uel_spjl);
		write_pjl  (sockfd, "@PJL");
		write_pjl  (sockfd, "@PJL USTATUSOFF");
		write_pjl  (sockfd, "@PJL INFO STATUS");
		// Get return
		write_pjl  (sockfd, "@PJL USTATUS DEVICE=ON");
		write_pjl  (sockfd, "@PJL SET JOBID=ON");
		write_pjl  (sockfd, "@PJL USTATUS JOB=ON");
		write_pjl  (sockfd, "@PJL JOB NAME=\"%s\"", pjob);
		// Get return
		write_pjl  (sockfd, "@PJL USTATUS PAGE=ON");
		write_data (sockfd, pjl_uel_spjl);
		// write_pjl  (sockfd, "@PJL USTATUS PAGE=ON");
		write_pjl  (sockfd, "@PJL USTATUS PAGE=ON");

		if (endpage <= 0) {
			write_pjl  (sockfd,
				"@PJL JOB NAME = \"User: %s; Job: %s\" "
				"START = %d",
				username, jobname, startpage);
		} else {
			write_pjl  (sockfd,
				"@PJL JOB NAME = \"User: %s; Job: %s\" "
				"START = %d END = %d",
				username, jobname, startpage, endpage);
		}
		// Get return
		write_pjl  (sockfd, "@PJL SET COPIES = 1");
		write_pjl  (sockfd, "@PJL ENTER LANGUAGE=POSTSCRIPT");

		fprintf (stdxx, "[Sending data]\n");
		my_state  = 1;

		do {
			pgb = (pgb + 1) % 4;

			if (verbose > 2) {
				fprintf (stderr, "\r%c", prog_banner[pgb]);
			}

			if (user_canceled) break;

			if (check_user_canceled) {
				check_user_canceled = 0;

				if (is_user_canceled ()) break;
			}
#if ENABLE_MYSQL_SUPPORT == 1
			timer->start (timer);
#endif
			i = j = 0;

			while (len > 0) {
				if ((j = write (sockfd, &buffer[i], len)) <= 0){
					break;
				}
				len -= j;
				i   += j;
			}

#if ENABLE_MYSQL_SUPPORT == 1
			if (timer->elapsed (timer) > 10000) {
				check_user_canceled = 1;
			}
#endif

			my_state = 3;
			read_data (sockfd, 10);

			if (endpage > 0 && endpage >= number_of_pages) {
				fprintf (stdxx, "End page found\n");
				break;
			}
		} while ((len = read (fd, buffer, sizeof buffer)) > 0);

		my_state = 4;

		read_data (sockfd, 5000);

		fprintf (stdxx, "\n[Finish sending data]\n");
		close(fd);


		write_data (sockfd, escape_E);
		write_data (sockfd, pjl_uel_spjl);
		write_pjl  (sockfd, "@PJL");
		write_pjl  (sockfd, "@PJL RESET");
		my_state = 5;
		write_pjl  (sockfd, "@PJL EOJ NAME = \"User: %s; Job: %s\"",
				username, jobname);
		// Receive @PJL USTATUS PAGE
		// Receive @PJL USTATUS JOB
		write_data (sockfd, pjl_uel_spjl);
		write_data (sockfd, pjl_uel_spjl);
		write_pjl  (sockfd, "@PJL EOJ NAME=\"%s\"", pjob);
		// Receive @PJL USTATUS JOB

		write_pjl  (sockfd, "@PJL INFO STATUS");

		// read_status_job (sockfd);

		do {
			signal (SIGALRM, interrupt);
			alarm (120);

			while (! get_final_result) {
				read_data (sockfd, 5000);
			}

			if (get_final_result == 3) {
				// *time is up
				get_final_result = 0;

				// is_user_canceled ();
#if ENABLE_SNMP_SUPPORT == 1
				if (snmp != NULL) {
					if (printer_status () == 0) {
						get_final_result = 4;
					}
				}
#else
				write_pjl  (sockfd, "@PJL INFO STATUS");
#endif
			}
		} while (! get_final_result);

		write_data (sockfd, pjl_uel_spjl);
		write_pjl  (sockfd, "@PJL USTATUSOFF");
		read_data (sockfd, 5000);
	}

	close(sockfd);

	if (logfp != NULL) fclose (logfp);

#if ENABLE_SNMP_SUPPORT == 1
	if (snmp != NULL) {
		int	loop;

		for (loop = 0; loop < 10; loop++) {
			if (printer_status () <= 0) break;
			sleep (10);
		}

		if (printer_status () >= 0) {
			counter2 = snmp->get_int32 (snmp, MIBs_GET_PAGECNT);

			fprintf (stdxx, "SNMP: [%d page printed]\n",
					counter2 - counter1);
		} else {
			have_cnt = 0;
		}
	}
#endif
	fprintf (stdxx, "[%d page printed]\n", number_of_pages);

#if ENABLE_SNMP_SUPPORT == 1
	if (have_cnt && get_final_result == 4) {
		int	cnt;

		if ((cnt = counter2 - counter1) > number_of_pages) {
			number_of_pages = cnt;

			fprintf (stdxx, "[Fix: %d page printed]\n",
					number_of_pages);
		}
	}
#endif

#if ENABLE_MYSQL_SUPPORT == 1
	mydb->query (mydb,
		"UPDATE pquota SET pquota=pquota-%d,lastuse=NOW() "
		"WHERE account='%s'",
		number_of_pages * ntdpp, user_acct
	);

	mydb->query (mydb,
		"INSERT INTO uselog (account,qid,pprint,unituse,"
		"logdate,ip,banner) "
		"VALUES ('%s',%d,%d,%d,NOW(),'%s','%s')",
		user_acct, my_queue_id,
		number_of_pages, number_of_pages * ntdpp,
		client_ip, p_banner
	);

	mydb->query (mydb,
		"DELETE FROM pqjob WHERE sn = %d", pq_sn
	);

	mydb->query (mydb,
		"DELETE FROM spooler WHERE serial_no = %d", spo_sn
	);

	if (file_to_print != NULL) {
		unlink (file_to_print);
	}


	request_or_free_printer (0);

	mydb->dispose (mydb);
#endif
#if ENABLE_SNMP_SUPPORT == 1
	if (snmp != NULL) snmp->dispose (snmp);
#endif

	fclose (stdxx);

#if ENABLE_MYSQL_SUPPORT == 1
	timer->dispose (timer);
#endif

	return 0;
}
