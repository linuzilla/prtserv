/* lpcloned.c      written by  Jiann-Ching Liu */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pwd.h>
#include <ctype.h>

#include "sys_conf.h"
#include "db_mysql.h"
#include "base64.h"


#define DEFAULT_THRESHOLD_RATE   (5)
#define DEFAULT_TIME_OUT_VAL     (600)
#define DEFAULT_USERNAME         "prtserv"
#define DEFAULT_SPOOL_DIR        "/prtserv/queue"


#ifndef Solaris
pid_t wait3(int *statusp, int options, struct rusage *rusage); 
#endif

struct sysconf_t		*sysconfig = NULL;
struct db_mysql_t		*mydb  = NULL;


#ifdef MULTI_THREADING

#include <thread.h>
#include <synch.h>
typedef void *(*thr_routine)(void *);
int    num_of_thread = 1;
#endif

#define BUFFSIZE	(4096)

#define LOG_ERROR	(1)
#define LOG_WARN	(2)
#define LOG_JOB		(1)
#define LOG_CONNECT	(2)
#define LOG_NOTIFY	(3)
#define LOG_PERFORMANCE (4)
#define LOG_FORDEBUG	(5)
#define LOG_EXTRA       (7)

char*  getClientName(int, struct sockaddr_in *); 
int    local_domain(const char* host);
void   processing(const int *sockptr);
int    open_socket(int port, void (*function)(const int*));
void   send_ok(int sock);
void   send_error(int sock);
// int    gethostname(char *name, int namelen);
int    Daemon_command(const int sockfd, char *queue);
int    Receive_job_subcommand(const int sockfd, const char *buffer);
void   create_and_write_file(char *, const int, const char *, const char *, const int);
int    readfile(char *, const int, const int, const long long);
int    get_job_and_username(const char *file, char *jname,
						char *uname, char *hname);
void   do_log(const int level, char *format, ...);
void   notify(const char *subject, const char *format, ...);

#ifndef Solaris
void   reapchild(int sig);
#endif

const  char *sendmail = "/usr/lib/sendmail";
static char *prtmanager = "printermanager";

int    verbose        = 0;
int    veryverbose    = 0;
int    mostverbose    = 0;
int    debug          = 0;
int    daemon_flag    = 0;
int    logx           = 0;
int    restrict_host  = 0;
int    keepUnnamefile = 0;
int    queueid        = 0;
int    job_id         = 0;
int    giveup         = 0;
char   *username      = NULL;
char   *spooldir      = NULL;
char   *myname;
uid_t  uid, gid;
uid_t  orguid, orggid;
int    threshold_rate = DEFAULT_THRESHOLD_RATE;
int    timeout        = DEFAULT_TIME_OUT_VAL;

void   interrupt(int);

int main(int argc, char *argv[]) {
	int		c, errflag = 0;
	int		port = 515;
	char		*ptr;
	struct passwd	*pwd;
	char		*cfgfile = NULL;
	int		user_opt = -1;

	myname = ((ptr = strrchr (argv[0], '/')) != NULL) ? ptr + 1 : argv[0];

	while ((c = getopt (argc, argv, "Ddf:hkl:p:Rr:t:u:v")) != EOF) {
		switch (c) {
		case 'f':
			cfgfile = optarg;
			break;
		case 'u':
			username = optarg;
			break;
		case 'D':
			daemon_flag = 1;
			break;
		case 'R':
			restrict_host = 1;
			break;
		case 'l':
			logx = atoi(optarg);
			break;
		case 'd':
			debug = 1;
			break;
		case 'k':
			keepUnnamefile = 1;
			break;
		case 'v':
			mostverbose = veryverbose;
			veryverbose = verbose;
			verbose = 1;
			break;
		case 'p':
			if (isdigit (optarg[0])) {
				port = atoi (optarg);
			} else {
				struct servent  *serv;

				if ((serv = getservbyname (
						optarg, "tcp")) != NULL) {
					port = serv->s_port;
				} else {
					printf("unknow services: %s\n", optarg);
					exit (1);
				}
			}
			break;
		case 'r':
			threshold_rate = atoi (optarg);
			break;
		case 't':
			timeout = atoi (optarg);
			break;
		case 'h':
		default:
			errflag++;
			break;
		}
	} 

	if (errflag) {
		fprintf (stderr,
			"usage: %s [options]\n"
			"options are:\n"
			"   -f cfgfile   config file\n"
			"   -D           become daemon\n"
                        "   -d           debug\n"
                        "   -k           keep unnamed file\n"
                        "   -v           verbose\n"
                        "   -vv          very verbose\n"
                        "   -u user      default = %s\n"
                        "   -l loglevel  default = 0\n"
                        "   -p port      default = 515\n"
                        "   -t timeout   default = %d sec.\n"
                        "   -r rate      threshold rate (default %d KB/sec.)\n",
			myname,
                        DEFAULT_USERNAME,
                        DEFAULT_TIME_OUT_VAL,
                        DEFAULT_THRESHOLD_RATE);
		exit (1);
	} 

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
	} else {
		if (username == NULL) {
			if ((username = sysconfig->getstr ("run-as")) == NULL) {
				username = DEFAULT_USERNAME;
			}
		}

		if (spooldir == NULL) {
			if ((spooldir = sysconfig->getstr (
							"spool-dir")) == NULL) {
				spooldir = DEFAULT_SPOOL_DIR;
			}
		}
	}

	if ((pwd = getpwnam (username)) == NULL) {
		fprintf (stderr, "%s: unknow user\n", username);
		exit (1);
	}

	uid = pwd->pw_uid;
	gid = pwd->pw_gid;

	if (daemon_flag) {
		int   i;

		if (fork()) {	/* 1. Call fork and have the parent exit  */
			// printf("%s installed\n", myname);
			exit(0);
		} 
		setsid ();	/* 2. Call setsid to create a new session  */
		// chdir("/");	/* 3. change the current working directory */
		chdir (DEFAULT_SPOOL_DIR);
				/* 3. change the current working directory */
		umask(0);	/* 4. set the file mode creation mask to 0 */
		/* 5. Close all open file descriptor    */
		for (i = 0; i < NOFILE; i++) close(i);
		/* 6. Ignore Terminal I/O Signals       */ 
        	signal(SIGTTOU, SIG_IGN);

		do_log (LOG_NOTIFY, "%s started", myname);
	}

	if ((mydb = new_dbmysql ()) == NULL) {
		perror ("new_dbmysql");
		return 0;
	}

	signal (SIGALRM, interrupt);
	signal (SIGINT,  interrupt);
	signal (SIGHUP,  interrupt);
	signal (SIGTERM, interrupt);
	signal (SIGCHLD, SIG_IGN);

#ifdef Solaris
	signal (SIGCLD,  SIG_IGN);
#else
	signal (SIGCHLD, reapchild);
#endif

	orguid = getuid ();
	orggid = getgid ();

	srand (getpid () + getppid ());



	if (open_socket (port, processing)) return 1;

	mydb->dispose (mydb);

	return 0;
}

int Daemon_command(const int sockfd, char *queue) {
    char buffer[BUFFSIZE];
    int  i, len;

    if ((len = read(sockfd, buffer, BUFFSIZE)) > 0) {
        for (i = 0; i < len; i++)
            if (buffer[i] == '\n')
                buffer[i] = '\0';
        strcpy(queue, &buffer[1]);
        return buffer[0];
    } else {
        return 0;
    }
}

int readfile (char *fname, const int sockfd,
		const int ctrlfile, const long long size) {
	char	*ptr;
	int	ln, lnp = 0, len = size;
	int	retval = 1;
	time_t	start_time;


	if (veryverbose) printf ("malloc %lld ...... ", size);

	if((ptr = malloc(size)) == NULL) {
		if (veryverbose) printf("error (%s:%d)\n", __FILE__, __LINE__);

		retval = 0;
	} else {
		if (veryverbose) printf("ok\n");

		start_time = time(NULL);

        	while (len > 0) {
			alarm(timeout);

			// if (mostverbose) printf("read %d bytes ..... ", len);

			if ((ln = read (sockfd, &ptr[lnp], len)) > 0) {
				lnp += ln;
				len -= ln;
				write (sockfd, "", 1);
				//if (mostverbose)printf("%d bytes read\n", ln);
			} else if (ln < 0) {
				if (mostverbose) printf ("%s:%d :%d:[%d] %s\n",
						__FILE__, __LINE__,
						ln, errno, strerror (errno));
				retval = 0;
				giveup = 1;
				break;
			} else if (ln == 0) {
				break;
			}
		}
		alarm(0);

		if (! ctrlfile) {
			double	elapsed_time =
					difftime(time(NULL), start_time);
			double	kbps;

			if (elapsed_time > 0.) {
				kbps  = (double) size / elapsed_time / 1024.;

				do_log (LOG_PERFORMANCE,
					"Transmition Rate = %3.2f KB/sec.",
					kbps);

				if (kbps < threshold_rate) {
					notify ("Need Attention !!", 
					"Transmition rate between CHARON "
					"and PRTSERV fall down to %3.2f KB/sec.\n",
					kbps);
				}
			}
		}

		if (veryverbose) printf("reading ok\n");

		if (veryverbose && ctrlfile) write(1, ptr, size);

		create_and_write_file (fname, ctrlfile, spooldir, ptr, lnp);

		free(ptr);
	}

	return retval;
}

static int add_print_job (char *uname, char *jname,
				char *hname, const char *file) {
	char	user[1024];
	char	jobn[1024];
	char	hnbf[1024];

	base64_encode (uname, strlen (uname), user, sizeof user);
	base64_encode (jname, strlen (jname), jobn, sizeof jobn);
	base64_encode (hname, strlen (hname), hnbf, sizeof hnbf);

	mydb->query (mydb,
		"UPDATE pqjob SET puser='%s',"
		"banner='%s',rhost='%s',q_file='%s',"
		"qj_state=1 WHERE sn=%d AND qj_state=0",
		user, jobn, hnbf, file, job_id);

	return 1;
}

static void remove_print_job (void) {
	if (job_id == 0) return;

	mydb->query (mydb,
		"DELETE FROM pqjob WHERE sn=%d AND qj_state=0",
		job_id
	);
}

int Receive_job_subcommand (const int sockfd, const char *queue) {
	char		buffer[BUFFSIZE], name[100];
	int		len, count;
	long long	dcount;
	char		datafile[256];
	char		ctrlfile[256];
	char		username[500], jobname[500], hname[500];

	datafile[0] = ctrlfile[0] = '\0';

	while ((len = read (sockfd, buffer, BUFFSIZE)) > 0) {
		buffer[len] = '\0';

		if (mostverbose) printf("%d [%s]\n", buffer[0], &buffer[1]);

		//for (i = 0; i < len; i++) {
			//  if (buffer[i] == '\n') buffer[i] = '\0';
		//}


		switch (buffer[0]) {
		case 0:
			if (verbose) printf ("OK\n");
			break;

		case 1: /* Abort job              01  LF                   */
			if (verbose) printf ("abort job\n");
			do_log (LOG_FORDEBUG, "abort job");
			send_ok (sockfd);
			break;

		case 2: /* Receive control file   02  Count  SP  Name  LF  */
			// printf ("Control [%s]\n", &buffer[1]);
			sscanf (&buffer[1], "%d %s", &count, name);
			if (verbose) {
				printf ("Receive control file %s (%d)\n",
						name, count);
			}

			do_log (LOG_FORDEBUG, "receive control file %s (%d)",
				       	name, count);
			send_ok (sockfd);

			readfile (ctrlfile, sockfd, 1, count);
			send_ok (sockfd);
			break;

		case 3: /* Receive data file      03  Count  SP  Name  LF  */
			// printf ("Data [%s]\n", &buffer[1]);
			sscanf (&buffer[1], "%llu %s", &dcount, name);

			if (verbose) {
				printf ("Receive data file %s (%lld)\n",
						name, dcount);
			}
			do_log (LOG_FORDEBUG,
				"receive data file %s (%lld)", name, dcount);

			if (dcount > 100000000) {
				send_error (sockfd);
				giveup = 2;
				break;
			}

			send_ok (sockfd);

			readfile (datafile, sockfd, 0, dcount);
			send_ok (sockfd);
			break;

		default:
			if (verbose) {
				printf ("%s:%d unknow command %d\n",
						__FILE__, __LINE__, buffer[0]);
			}
			// send_error (sockfd);
			break;
		}
	}


	if (giveup || (datafile[0] == '\0') || (ctrlfile[0] == '\0')) {
		remove_print_job ();

		if (datafile[0] != '\0') unlink (datafile);
		if (ctrlfile[0] != '\0') unlink (ctrlfile);

		if (giveup == 1) {
			do_log (LOG_JOB, "user cancel ... deleted");
		} else {
			do_log (LOG_JOB, "over size");
		}
	} else if (get_job_and_username (ctrlfile, jobname, username, hname) ||
							keepUnnamefile) {
		add_print_job (username, jobname, hname, datafile);

		if (debug) {
			printf ("Data file = %s\nControl file = %s\n",
					datafile, ctrlfile);
			printf ("Jobname = %s\nUsername = %s\n",
					jobname, username);
		}

		do_log (LOG_JOB, "U=%s J=%s", username, jobname);
	} else {
		remove_print_job ();

		unlink(datafile);
		do_log (LOG_JOB, "missing control file ... deleted");
	}

	return 1;
}


int get_job_and_username (const char *file,
				char *jname, char *uname, char *hname) {
	FILE	*fp;
	char	buffer[256];
	int	i;

	// fprintf (stderr, "[%s]\n", file);
	if ((fp = fopen(file, "r")) != NULL) {
		while (fgets (buffer, sizeof buffer -1, fp) != NULL) {
			buffer[sizeof buffer - 1] = '\0';

			for (i = strlen (buffer) - 1; i > 0; i--) {
				if (buffer[i] == '\r' || buffer[i] == '\n') {
					buffer[i] = '\0';
				} else {
					break;
				}
			}

			switch (buffer[0]) {
			case 'H':
				strcpy (hname, &buffer[1]);
				break;
			case 'J':
				strcpy (jname, &buffer[1]);
				break;
			case 'P':
				strcpy (uname, &buffer[1]);
				break;
			default:
				break;
			}

			// do_log(LOG_EXTRA, buffer);
		}

		if (jname[0] == '\0') strcpy(jname, "<undef>");
		if (uname[0] == '\0') strcpy(uname, "<undef>");
		if (hname[0] == '\0') strcpy(hname, "<undef>");

		fclose (fp);
		unlink (file);
		return 1;
	} else {
		strcpy (jname, "(undef)");
		strcpy (uname, "(undef)");
		strcpy (hname, "(undef)");

		return 0;
	}
}

void processing (const int *sockptr) {
	int			sockfd = *sockptr;
	int			cmd;
	char			buffer[BUFFSIZE];
	struct sockaddr_in	clientAddr;
	char			remotehostname[60], remoteip[17];
	int			remoteport;
	int			ipx[4];
	int			found = 0;
	char			**result;

	giveup = 0;

	if (mydb->connect (mydb,
			sysconfig->getstr ("mysql-server"),
			sysconfig->getstr ("mysql-user"),
			sysconfig->getstr ("mysql-password"),
			sysconfig->getstr ("mysql-database"))) {
	} else {
		mydb->perror (mydb, "mysql");
		return;
	}

	strcpy (remotehostname, getClientName(sockfd, &clientAddr));
	remoteport = ntohs (clientAddr.sin_port);

	strcpy (remoteip, inet_ntoa (clientAddr.sin_addr));
	sscanf (remoteip, "%d.%d.%d.%d", &ipx[0], &ipx[1], &ipx[2], &ipx[3]);

	if (restrict_host) {
		mydb->query (mydb,
			"SELECT sn,yn FROM hosts_allow WHERE ip = '%s'",
			remoteip);

		if ((result = mydb->fetch (mydb)) != NULL) {
			if (result[1][0] == '1') found = 1;
		} else {
			mydb->query (mydb,
				"SELECT sn,yn FROM hosts_allow WHERE ip="
				"'%d.%d.%d.'",
				ipx[0], ipx[1], ipx[2]);

			if ((result = mydb->fetch (mydb)) != NULL) {
				if (result[1][0] == '1') found = 1;
			} else {
				mydb->query (mydb,
					"SELECT sn,yn FROM hosts_allow "
					"WHERE ip="
					"'%d.%d.'",
					ipx[0], ipx[1]);

				if ((result = mydb->fetch (mydb)) != NULL) {
					if (result[1][0] == '1') found = 1;
				}
			}
		}

		if (found) {
			mydb->query (mydb,
				"UPDATE hosts_allow SET lastuse=NOW(),"
				"usecnt=usecnt+1 WHERE sn='%s'", result[0]);
		}

		mydb->free_result (mydb);
	} else {
		found = 1;
	}

//  if (remoteport >= 1024 || ! found) {
	if (! found) {
		// const char *permissiondeny = "Permission Deny !!\n";

		if (verbose) {
			printf ("Refuse connect form: %s (%d)\n",
					remotehostname, remoteport);
		}

		do_log (LOG_CONNECT, "Refuse connect from: %s (%d)\n",
                             remotehostname, remoteport);
		// write (sockfd, permissiondeny, strlen (permissiondeny) + 1);
#ifdef MULTI_THREADING
		close (sockfd);
		num_of_thread--;
		/*      thr_exit(0);  */
#endif
		return;
	}

	if (verbose) {
		printf ("Connect from: %s (%d)\n", remotehostname, remoteport);
	}

	do_log (LOG_CONNECT,
			"Connect from: %s (%d)", remotehostname, remoteport);

	queueid = 0;
	job_id = 0;

	switch ((cmd = Daemon_command(sockfd, buffer))) {
	case 1: /* Print any waiting jobs  */
		if (verbose) printf ("Print any waiting jobs (%s)\n", buffer);
		do_log (LOG_FORDEBUG, "Print any waiting jobs (%s)", buffer);
		send_ok (sockfd);
		break;

	case 2: /* Receive a printer job   */

		if (verbose) printf ("Receive a printer job (%s)\n", buffer);
		do_log (LOG_FORDEBUG, "Receive a printer job (%s)", buffer);

		mydb->query (mydb,
			"SELECT qid FROM pqueue WHERE qname = '%s'"
			" AND pq_state=1", buffer
		);

		if ((result = mydb->fetch (mydb)) != NULL) {
			queueid = atoi (result[0]);

			if (verbose) printf ("Queue id = %d\n", queueid);

			mydb->query (mydb,
				"INSERT INTO pqjob (qid,ip,inq_date) "
				"VALUES (%d,'%s',NOW())", queueid, remoteip);

			mydb->query (mydb, "SELECT LAST_INSERT_ID()");

			if ((result = mydb->fetch (mydb)) != NULL) {
				job_id = atoi (result[0]);
			} else {
				job_id = 0;
			}
		} else {
			fprintf (stderr, "Queue [%s] not found\n", buffer);
			queueid = 0;
			job_id = 0;
		}

		mydb->free_result (mydb);

		send_ok (sockfd);
		Receive_job_subcommand (sockfd, buffer);

		break;

	case 3: /* Send queue stat (short) */
		if (verbose) printf ("Send queue state (short) (%s)\n", buffer);
		do_log (LOG_FORDEBUG, "Send queue state (short) (%s)", buffer);
		send_ok (sockfd);
		break;

	case 4: /* Send queue stat (long)  */
		if (verbose) printf ("Send queue state (long) (%s)\n", buffer);
		do_log (LOG_FORDEBUG, "Send queue state (long) (%s)", buffer);
        	send_ok (sockfd);
		break;

	case 5: /* Remove jobs             */
		if (verbose) printf ("Remove jobs (%s)\n", buffer);
		do_log (LOG_FORDEBUG, "Remove jobs (%s)", buffer);
		send_ok (sockfd);
		break;

	case 0: /* Unknow command */
		if (verbose) printf ("Command 0\n");
		send_ok(sockfd);
		break;

	default:
		if (verbose) printf ("unknow command %d (%s)\n", cmd, buffer);
		do_log (LOG_FORDEBUG, "unknow command %d (%s)", cmd, buffer);
		break;
	}

	if (verbose) printf ("connection closed !\n");

	do_log (LOG_CONNECT, "connection closed !");

#ifdef MULTI_THREADING
	close (sockfd);
	num_of_thread--;
	/*  thr_exit(0);  */
#endif
	return;
}

int open_socket (int port, void (*function)(const int*)) {
    int                 sock, length, msgsock;
    struct sockaddr_in  server;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0))< 0) {
        perror("opening stream socket");
        return 1;
    }

    server.sin_family      = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port        = htons(port);

    if (bind(sock, (struct sockaddr*)&server, sizeof server) < 0) {
        perror("binding stream socket");
        return 1;
    }

    length = sizeof server; 

    listen(sock, 5);
    do {
        msgsock = accept(sock, (struct sockaddr*) 0, (int*) 0);
        if (msgsock == - 1) {
            do_log(LOG_ERR, "error: accept");
        } else {
#ifdef MULTI_THREADING
            int   thr_id;

            if (thr_create(NULL, 0, (thr_routine) function, &msgsock, 
                             THR_DETACHED, &thr_id)) {
                ++num_of_thread;

                if (debug)
                    printf("thr_create failed !!\n");

                function(&msgsock);
            } else {
                thr_setconcurrency(++num_of_thread);
                if (debug)
                    printf("thr_create(%d), thr_setconcurrency(%d)\n",
                            thr_id, num_of_thread);
            }
#else
            int   pid;

            switch(pid = fork()) {
            case -1:
                do_log(LOG_WARN, "Can't fork child process !!");
		/*
                setgid(gid);
                setuid(uid);
                function(&msgsock);
                setuid(orguid);
                setgid(orggid);
		*/
                break;
            case  0: /* child process */
                setgid(gid);
                setuid(uid);
                function(&msgsock);
                exit(0);
                break;
            default: /* parent */
                break;
            }
            close(msgsock);
#endif
        }
    } while (1);

    return 0;
}

void send_ok(int sock) {
	static char  c = '\0';
	write(sock, &c, 1);
}

void send_error(int sock) {
	static char  c = '\xff';
	write(sock, &c, 1);
}

char* getClientName(int sock, struct sockaddr_in *clientAddr)
{
/*  struct sockaddr_in  clientAddr;  */
    struct hostent      *hp = NULL;
    static char         remotehost[100];
    int                 on, len = sizeof(struct sockaddr_in);

    if (getpeername(sock, (struct sockaddr *) clientAddr, &len) == -1) {
        perror("getpeername");
        exit(1);
    }

    /*    port = ntohs(clientAddr.sin_port);  */

    on = 1; setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    on = 1; setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on));

    hp = gethostbyaddr((char*) & clientAddr->sin_addr, sizeof(struct in_addr),
                                 clientAddr->sin_family);

    if (hp) {
        /* if the name returned by gethostbyaddr() is in our domain,  */
        /* attempt to verify that we haven't been fooled by someone   */
        /* in a remote net. Look up the name and check that           */
        /* this address corresponds to the name.                      */
        if (local_domain(hp->h_name)) {

            strncpy(remotehost, hp->h_name, sizeof(remotehost) - 1);
            remotehost[sizeof(remotehost) - 1] = '\0';

            if ((hp = gethostbyname(remotehost)) == NULL) {
                /* syslog couldn't look up address for remotehost; */
                return inet_ntoa(clientAddr->sin_addr);
            }

            for ( ; ; hp->h_addr_list++) {
                if (bcmp(hp->h_addr_list[0], (caddr_t) &clientAddr->sin_addr,
                         sizeof(clientAddr->sin_addr)) == 0)
                    break;
                if (hp->h_addr_list[0] == NULL) {
                    /* syslog Host addr not listed for host  */
                    return inet_ntoa (clientAddr->sin_addr);
                } 
            }
        }
        strcpy(remotehost, hp->h_name);
        return remotehost;
    } else {
        return inet_ntoa(clientAddr->sin_addr);
    } 
}

int local_domain(const char* host)
{
    char           *ptr1, *ptr2;
    char           localhost[100];

    if ((ptr1 = (char *) index(host, '.')) == NULL)
        return 1;

    gethostname(localhost, sizeof(localhost));

    if ((ptr2 = (char *) index(localhost, '.')) == NULL)
        return 1;

    if (strcasecmp(ptr1, ptr2) == 0)
        return 1;
    return 0;
}

void notify(const char *subject, const char *format, ...)
{
    FILE       *fd;
    char       smail[100];
    va_list    ap;

    sprintf(smail, "%s %s", sendmail, prtmanager);

    if ((fd = popen(smail, "w")) != NULL) {
        fprintf(fd, "To: %s\n", prtmanager);
        fprintf(fd, "Subject: %s\n\n", subject);
        va_start(ap, format);
        vfprintf(fd, format, ap);
        va_end(ap);
        pclose(fd);
    } 
}

void interrupt(int signo)
{
    signal(signo, interrupt);

    switch (signo) {
    case SIGALRM:
        alarm(0);
        do_log(LOG_NOTIFY, "Alarm Clock (%d sec)", timeout);
        notify("Transmition Time Out !!",
               "Transmition time out between CHARON and PRTSERV.\n");
        break;
    case SIGHUP:
        // do_log(LOG_NOTIFY, "reload %s", hostfile);
        break;
    case SIGINT:
    case SIGTERM:
        do_log(LOG_NOTIFY, "%s killed (%d)", myname, signo);
        printf("Killed !!\n");
        exit(1);
    default:
        break;
    }
}

void create_and_write_file (char *filepath, const int ctrlfile,
			const char *path, const char *buf, const int size) {
	int	fd;

	if (job_id == 0) return;

	if (ctrlfile) {
		sprintf (filepath, "%s/p%d-%09d.txt", path, queueid, job_id);
	} else {
		sprintf (filepath, "%s/p%d-%09d.prn", path, queueid, job_id);
	}

	if ((fd = open (filepath, O_CREAT|O_WRONLY|O_TRUNC, 0664)) >= 0) {
    		write (fd, buf, size);
    		close (fd) ; 
	}
}

void do_log(const int level, char *format, ...)
{
    char     logmsg[512];
    va_list  ap;

    if (level > logx) 
        return;

    va_start(ap, format);
    vsprintf(logmsg, format, ap);
    va_end(ap);

    /*   daemon.notice                          */
    /*   openlog(myname, LOG_PID, LOG_LOCAL7);  */
    openlog(myname, LOG_PID, LOG_LPR);
    /* syslog(LOG_NOTICE, "%s: %m", logmsg);    */
    syslog(LOG_INFO, "%s", logmsg);
    closelog();
} 

#ifndef Solaris

void reapchild(int sig)
{
    int      pid;
    int      status;

    while ((pid = wait3(&status, WNOHANG, (struct  rusage *) 0)) > 0)
        ;

    return;
}

#endif
