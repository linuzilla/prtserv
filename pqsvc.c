/* pqsvc.c      written by  Jiann-Ching Liu */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/signal.h>
#include <signal.h>

#include "sys_conf.h"
#include "db_mysql.h"


struct varlist_t {
	char	*var;
	void	(*func)(const int);
};

static struct sysconf_t		*sysconfig = NULL;
static struct db_mysql_t	*mydb = NULL;
static volatile short		terminate = 0;
static volatile short		do_it = 0;
static int			sleep_time = 60;
static short			need_lprm = 0;
static int			queryqu[3] = { 0, 0, 0 };
static int			cancelq[3] = { 0, 0, 0 };


#define DEFAULT_USERNAME	"prtserv"

static int	my_signal = SIGUSR1;

static void set_sleep_time (const int seconds) { sleep_time = seconds; }

static void lp_q_rm (const int i, const int rm, const int qid) {
	if (rm) {
		if ((cancelq[i] = qid) != 0) need_lprm = 1;
	} else {
		if ((queryqu[i] = qid) != 0) need_lprm = 1;
	}
}

static void lprm_1 (const int qid) { lp_q_rm (0, 1, qid); }
static void lprm_2 (const int qid) { lp_q_rm (1, 1, qid); }
static void lprm_3 (const int qid) { lp_q_rm (2, 1, qid); }

static void lpq_1 (const int qid) { lp_q_rm (0, 0, qid); }
static void lpq_2 (const int qid) { lp_q_rm (1, 0, qid); }
static void lpq_3 (const int qid) { lp_q_rm (2, 0, qid); }

static struct varlist_t		vlist[] = {
	{ "sleep",	set_sleep_time },
	{ "lprm-1",	lprm_1 },
	{ "lprm-2",	lprm_2 },
	{ "lprm-3",	lprm_3 },
	{ "lpq-1",	lpq_1  },
	{ "lpq-2",	lpq_2  },
	{ "lpq-3",	lpq_3  }
};

static void interrupt (const int signo) {
	signal (signo, interrupt);

	if (signo == my_signal) {
		// fprintf (stderr, "\n*** My Signal ***\n");
		do_it++;
	} else {
		fprintf (stderr, "\n*** Break ***\n");
		terminate = 1;
	}
}

int main (int argc, char *argv[]) {
	FILE		*fp;
	int		c, errflag = 0;
	char		*ptr;
	char		*cfgfile = NULL;
	int		user_opt = -1;
	char		*myname = NULL;
	char		**proglist = NULL;
	int		proglen = 0;
	int		psvc = 0;
	int		num  = 0;
	int		daemon_flag = 0;
	int		test_flag = 0;
	char		**result;
	char		*pname = NULL;
	char		*basedir = "/usr/local";
	char		program[512];
	char		*username = NULL;
	struct passwd	*pwd;
	char		pathenv[2048];
	char		*pidfile = "/tmp/pqsvc.pid";
	short		no_sleep;
	int		i, sn, n_item;
	int		enable_autoprint = 1;

	n_item = sizeof vlist / sizeof (struct varlist_t);

	myname = ((ptr = strrchr (argv[0], '/')) != NULL) ? ptr + 1 : argv[0];

	while ((c = getopt (argc, argv, "df:hs:t")) != EOF) {
		switch (c) {
		case 't':
			test_flag = 1;
			break;
		case 'd':
			test_flag = 1;
			daemon_flag = 1;
			break;
		case 'f':
			cfgfile = optarg;
			break;
		case 's':
			sleep_time = atoi (optarg);
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
			"   -s sec       sleep time\n"
			"   -d           daemon\n",
			myname);
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
		char	*ptr;

		if ((ptr = sysconfig->getstr ("pidfile")) != NULL) {
			pidfile = ptr;
		}
	}

	if (! test_flag) {
		int	rc = 2, pid;

		if ((fp = fopen (pidfile, "r")) != NULL) {
			fscanf (fp, "%d", &pid);
			if (kill (pid, my_signal) == 0) {
				printf ("Send signal to %d\n", pid);
				rc = 0;
			} else {
				printf ("Fail send signal to %d\n", pid);
				rc = 1;
			}
			fclose (fp);
		}

		exit (rc);
	}

	if (username == NULL) {
		if ((username = sysconfig->getstr ("run-as")) == NULL) {
			username = DEFAULT_USERNAME;
		}
	}

	if ((pwd = getpwnam (username)) == NULL) {
		fprintf (stderr, "%s: unknow user\n", username);
		exit (1);
	}

	setregid (pwd->pw_gid, pwd->pw_gid);
	setreuid (pwd->pw_uid, pwd->pw_uid);

	if ((proglist = sysconfig->strlist (
				"printer-program", &proglen)) != NULL) {
		/*
		for (i = 0; i < proglen; i++) {
			printf ("[%s]", proglist[i]);
		}
		*/
		printf ("\n");
	} else {
		fprintf (stderr, "missing 'printer-program' on config file\n");
		return 0;
	}

	if ((ptr = sysconfig->getstr ("basedir")) != NULL) {
		basedir = ptr;
	}

	snprintf (pathenv, sizeof pathenv - 1, "%s/bin:%s",
					basedir, getenv ("PATH"));
	pathenv[sizeof pathenv - 1] = '\0';
	setenv ("PATH", pathenv, 1);

	// fprintf (stderr, "[%s]\n", getenv ("PATH"));
	// exit (1);



	if (daemon_flag) {
		int   i;

		if (fork()) {	/* 1. Call fork and have the parent exit  */
			// printf("%s installed\n", myname);
			exit(0);
		} 
		setsid ();	/* 2. Call setsid to create a new session  */
		// chdir("/");	/* 3. change the current working directory */
		chdir ("/");
				/* 3. change the current working directory */
		umask (0);	/* 4. set the file mode creation mask to 0 */
		/* 5. Close all open file descriptor    */
		for (i = 0; i < NOFILE; i++) close(i);
		/* 6. Ignore Terminal I/O Signals       */ 
        	signal (SIGTTOU, SIG_IGN);
	}

	if ((fp = fopen (pidfile, "w")) != NULL) {
		fprintf (fp, "%d\n", getpid ());
		fclose (fp);
	}

	if ((mydb = new_dbmysql ()) == NULL) {
		perror ("new_dbmysql");
		return 0;
	}

	// if (daemon_flag) mydb->verbose (mydb, 2);

	signal (SIGALRM, SIG_IGN);
	signal (SIGINT,  interrupt);
	signal (SIGHUP,  SIG_IGN);
	signal (SIGTERM, interrupt);
	signal (SIGCHLD, SIG_IGN);
	signal (my_signal, interrupt);

	while (! terminate) {
		no_sleep = 0;

		if (mydb->connect (mydb,
			sysconfig->getstr ("mysql-server"),
			sysconfig->getstr ("mysql-user"),
			sysconfig->getstr ("mysql-password"),
			sysconfig->getstr ("mysql-database"))) {
		} else {        
			if (daemon_flag) {
				sleep (120);
				unlink (pidfile);
				execvp (argv[0], argv);
				continue;
			} else {
				mydb->perror (mydb, "mysql");
				unlink (pidfile);
				return 1;
			}
		}

		// ----------------------------------------------------

		mydb->query (mydb,
			"UPDATE pqsvc SET intval=2 WHERE "
			"variable='changed' AND intval=1"
		);

		if (mydb->affected_rows (mydb) > 0) {
			mydb->query (mydb, "SELECT variable,intval FROM pqsvc");

			while ((result = mydb->fetch (mydb)) != NULL) {
				for (i = 0; i < n_item; i++) {
					if (strcmp (result[0],
							vlist[i].var) == 0) {
						vlist[i].func (atoi(result[1]));
						break;
					}
				}
			}

			mydb->query (mydb,
				"UPDATE pqsvc SET intval=0 WHERE variable "
				"LIKE 'lprm-%%' AND intval != 0"
			);

			mydb->query (mydb,
				"UPDATE pqsvc SET intval=0 WHERE variable "
				"LIKE 'lpq-%%' AND intval != 0"
			);

			mydb->query (mydb,
				"UPDATE pqsvc SET intval=0 WHERE "
				"variable='changed'"
			);
		}

		if (need_lprm) {
			for (i = 0; i < 3; i++) {
				if (cancelq[i] == 0) continue;

				mydb->query (mydb,
					"SELECT printer.pname,pqueue.psvc "
					"FROM printer LEFT JOIN pqueue "
					"USING (qid) wHERE pid=%d",
					cancelq[i]
				);

				while ((result = mydb->fetch (mydb)) != NULL) {
					pname = result[0];
					psvc  = atoi (result[1]);

					if (psvc > proglen) continue;
					if (proglist[psvc] == NULL) continue;
					if (psvc != 1) continue;

					snprintf (program, sizeof program,
						"%s/bin/%s", basedir,
						proglist[psvc]);

					if (access (program, X_OK) != 0) {
					} else if (fork () == 0) {
						execl (program, program,
							"-wkp", pname, NULL);
						exit (0);
					}
				}

				cancelq[i] = 0;
			}

			for (i = 0; i < 3; i++) {
				if (queryqu[i] == 0) continue;

				mydb->query (mydb,
					"SELECT printer.pname,pqueue.psvc "
					"FROM printer LEFT JOIN pqueue "
					"USING (qid) wHERE pid=%d",
					queryqu[i]
				);

				while ((result = mydb->fetch (mydb)) != NULL) {
					pname = result[0];
					psvc  = atoi (result[1]);

					if (psvc > proglen) continue;
					if (proglist[psvc] == NULL) continue;
					if (psvc != 1) continue;

					snprintf (program, sizeof program,
						"%s/bin/%s", basedir,
						proglist[psvc]);

					if (access (program, X_OK) != 0) {
					} else if (fork () == 0) {
						execl (program, program,
							"-wQp", pname, NULL);
						exit (0);
					}
				}

				queryqu[i] = 0;
			}

			need_lprm = 0;
		}

		while (enable_autoprint) {
			mydb->query (mydb,
				"SELECT pqjob.sn,pqjob.ip,autoprint.account,"
				"autoprint.pid FROM pqjob "
				"LEFT JOIN autoprint USING (ip,qid) "
				"WHERE pqjob.account IS NULL AND qj_state=1 "
				"AND inq_date > stime"
			);

			if ((result = mydb->fetch (mydb)) != NULL) {
				sn = atoi (result[0]);

				mydb->query (mydb,
					"UPDATE pqjob SET qj_state=3 "
					"WHERE sn=%d AND ip='%s' "
					"AND qj_state=1",
					sn, result[1]
				);

				if (mydb->affected_rows (mydb) > 0) {
					mydb->query (mydb,
						"INSERT INTO spooler "
						"(sn,account,ptime,spst,pid) "
						"VALUES (%d,'%s',NOW(),0,%d) ",
						sn, result[2],
						atoi (result[3])
					);
				}
			} else {
				break;
			}
		}

		mydb->query (mydb,
			"SELECT printer.pname,pqueue.psvc,COUNT(*),"
			"RAND() AS rnd "
			"FROM spooler LEFT JOIN pqjob USING (sn) "
			"LEFT JOIN pqueue USING (qid) "
			"LEFT JOIN printer USING (qid) "
			"WHERE printer.pstate = 1 AND spooler.spst = 0 "
			"AND (spooler.pid = 0 OR printer.pid = spooler.pid) "
			"GROUP BY (pname) ORDER BY rnd"
		);

		while ((result = mydb->fetch (mydb)) != NULL) {
			pname = result[0];
			psvc  = atoi (result[1]);
			num   = atoi (result[2]);

			if (psvc > proglen) continue;

			if (proglist[psvc] != NULL) {
				snprintf (program, sizeof program,
					"%s/bin/%s", basedir, proglist[psvc]);

				if (! daemon_flag) {
					fprintf (stderr, "[%s][%s]\n",
						program, pname);
				}

				if (access (program, X_OK) != 0) {
					if (! daemon_flag) {
						fprintf (stderr,
							"[%s] not found\n",
							program);
					}
				} else if (fork () == 0) {
					if (daemon_flag) {
						execl (program, program,
							"-wp", pname, NULL);
						++do_it;
					} else {
						execl (program, program,
							"-wvvvp", pname, NULL);
						++do_it;
					}
					exit (0);
				} else {
					no_sleep = 1;
				}
			}

			/*
			fprintf (stderr, "[%s][%d][%d][%s][%s]\n",
				pname, psvc, num,
				proglist[psvc] == NULL ? "(NULL)" :
				proglist[psvc],
				basedir);
			*/
		}

		// ----------------------------------------------------

		mydb->disconnect (mydb);

		if (! daemon_flag) break;

		if (no_sleep) continue;

		if (do_it < 0) {
			sleep (sleep_time);
		} else {
			--do_it;
			sleep (1);
		}
		// fprintf (stderr, "Sleep for %d\n", sleep_time);
	}

	// fprintf (stderr, "dispose\n");
	mydb->dispose (mydb);
	unlink (pidfile);

	return 0;
}
