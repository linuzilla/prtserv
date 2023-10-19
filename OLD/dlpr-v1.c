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
#include "xtimer.h"

#ifndef ENABLE_MYSQL_SUPPORT
#define ENABLE_MYSQL_SUPPORT    1
#endif

#if ENABLE_MYSQL_SUPPORT == 1
#include "db_mysql.h"
#include "x_object.h"
#include "sys_conf.h"
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

#if ENABLE_MYSQL_SUPPORT == 1
static struct sysconf_t			*sysconfig = NULL;
static struct db_mysql_t		*mydb  = NULL;
static struct x_object_interface_t	*obji  = NULL;
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

int main (int argc, char *argv[]) {
	int	c, fd;
	int	errflag    = 0;
	char	*lpr_host  = "140.115.1.149";
	char	*lpr_queue = "no_name";
	char	*cfgfile = NULL;
#if ENABLE_MYSQL_SUPPORT == 1
	int	user_opt = 0;
#endif

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

	while ((c = getopt (argc, argv, "f:h:q:")) != EOF) {
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
		default:
			errflag++;
			break;
		}
	}

#if ENABLE_MYSQL_SUPPORT != 1
	if (optind >= argc || errflag) {
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

	obji = init_x_object_interface ();

	if ((mydb = new_dbmysql ()) == NULL) {
		perror ("new_dbmysql");
		return 0;
	} else {
		if (mydb->connect (mydb,
			sysconfig->getstr ("mysql-server"),
			sysconfig->getstr ("mysql-user"),
			sysconfig->getstr ("mysql-password"),
			sysconfig->getstr ("mysql-database"))) {
		} else {
			mydb->perror (mydb, "mysql");
			return 0;
		}
	}
#endif

#if ENABLE_MYSQL_SUPPORT == 1
#else
	if ((optind + 1 == argc) && (strcmp (argv[optind], "-") == 0)) {
		fd = STDIN_FILENO;
		do_lpr (lpr_host, lpr_queue, fd, "(stdin)", 0);
	} else {
		struct stat	stbuf;

		for (; optind < argc; optind++) {
			if ((fd = open (argv[optind], O_RDONLY)) >= 0) {
				if (fstat (fd, &stbuf) == 0) {
					if (do_lpr (lpr_host, lpr_queue, fd,
							argv[optind],
							stbuf.st_size) < 0) {
						optind = argc;
					}
				}
				close (fd);
			}
		}
	}
#endif

#if ENABLE_MYSQL_SUPPORT == 1
	mydb->dispose (mydb);
#endif
	timer->dispose (timer);

	return 0;
}

