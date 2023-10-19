/*
 *	raw9100.c
 *
 *	Copyright (c) 2004, Jiann-Ching Liu
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
// #include <getopt.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>


static struct sockaddr_in	issuer;
static char			*remote_host = "140.115.1.149";
static int			remote_port  = 9100;
static int			timeout_secs = 0;

void usage    (const char *prog);
void tcpproxy (const int fd);
void copyloop (const int insock, const int outsock);
int  writehex (const char *buf, const int bytes);

int main (int argc, char *argv[]) {
	int	c, errflg    = 0;
	int	local_port   = 9100;
	char	*program, *cp;
	int	inetd_flag = 0;

	int			socksvr, fd;
	struct sockaddr_in	serv_addr, client_addr;
	// fd_set		ready, testfds;
	// struct timeval	timeout;
	char			on = 1;

	program = ((cp = strrchr (argv[0], '/')) != NULL) ? cp + 1 : argv[0];

	while ((c = getopt (argc, argv, "ih:p:l:")) != EOF) {
		switch (c) {
		case  'i':
			inetd_flag = 1;
			break;
		case  'h':
			remote_host = optarg;
			break;
		case  'p':
			remote_port = atoi (optarg);
			break;
		case  'l':
			local_port = atoi (optarg);
			break;
		case  '?':
			errflg++;
			break;
		}
	}

	if (errflg) {
		usage (program);
		return 1;
	}

	if (inetd_flag) {
		tcpproxy (0);
		return 0;
	}

	if ((socksvr = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		perror ("socket");
		exit (1);
	}

	setsockopt (socksvr, SOL_SOCKET, SO_REUSEADDR, (char*) &on, sizeof on);
	setsockopt (socksvr, SOL_SOCKET, SO_KEEPALIVE, (char*) &on, sizeof on);

	bzero (&serv_addr, sizeof serv_addr);
	serv_addr.sin_family      = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port        = htons (local_port);

	if (bind (socksvr, (struct sockaddr *) &serv_addr,
						sizeof (struct sockaddr)) < 0) {
		perror ("bind");
		exit (1);
	} else {
		int	len = sizeof serv_addr;

		if (getsockname (socksvr, (struct sockaddr *) &serv_addr,
								&len) < 0) {
			perror ("getsockname");
			exit (1);
		}
	}

	printf ("Socket port=%d\n", ntohs (serv_addr.sin_port));

	listen (socksvr, 5);
	/*
	FD_ZERO (&ready);
	FD_SET (socksvr, &ready);

	while (1) {
		timeout.tv_sec = 5;

		if (select (FD_SETSIZE, &ready, (fd_set *) 0,
						(fd_set *) 0, &timeout) < 0) {
			perror ("select");
			continue;
		}

		for (fd = 0; fd < FD_SETSIZE; fd++) {
			if (FD_ISSET (fd, &testfds)) {
				if (fd == socksvr) {
			//		FD_SET ();
				}
			}
		}
	} 
	*/

	while (1) {
		int 	client_len;

		client_len = sizeof (client_addr);

		fd = accept (socksvr,
				(struct sockaddr *) &client_addr, &client_len);

		if (fd >= 0) tcpproxy (fd);
	}
}

void usage (const char *prog) {
	fprintf (stderr, "%s [-i][-h host][-p port][-l listen_port]\n", prog);
}

void tcpproxy (const int fd) {
	int			sockfd;
	struct hostent		*hp;
	struct sockaddr_in	rsvr_addr;
	int			len;

	len = sizeof issuer;
	if (getpeername (fd, (struct sockaddr *) &issuer, &len) < 0) {
		perror ("getpeername");
		close (fd);
		return;
	}

	printf ("connect from: %s\n", inet_ntoa (issuer.sin_addr));

	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		perror ("socket");
		return;
	}

	if ((hp = gethostbyname (remote_host)) == 0) {
		perror (remote_host);
		return;
	}

	bzero (&rsvr_addr, sizeof rsvr_addr);
	bcopy ((char*)hp->h_addr, (char*)&rsvr_addr.sin_addr, hp->h_length);
	rsvr_addr.sin_family = AF_INET;
	rsvr_addr.sin_port = htons (remote_port);

	if (connect (sockfd, (struct sockaddr *) &rsvr_addr, 
						sizeof rsvr_addr) < 0) {
		perror ("connect");
		return;
	}

	copyloop (fd, sockfd);
}

void copyloop (const int insock, const int outsock) {
	fd_set		iofds;
	fd_set		c_iofds;
	int		max_fd;		/* Maximum numbered fd used */
	struct timeval	timeout;
	unsigned long	bytes;
	unsigned long	bytes_in = 0;
	unsigned long	bytes_out = 0;
	unsigned int	start_time, end_time;
	char		buf[4096];

	/* Record start time */
	start_time = (unsigned int) time (NULL);

	/* Set up timeout */
	timeout.tv_sec = timeout_secs;
	timeout.tv_usec = 0;

	/* file descriptor bits */
	FD_ZERO (&iofds);
	FD_SET  (insock, &iofds);
	FD_SET  (outsock, &iofds);

    
	max_fd = (insock > outsock) ? insock : outsock;

	printf ("Entering copyloop() - timeout is %d\n", timeout_secs);

	while (1) {
		(void) memcpy (&c_iofds, &iofds, sizeof(iofds));

		if (select (max_fd + 1, &c_iofds, (fd_set *)0,
			(fd_set *)0, (timeout_secs ? &timeout : NULL)) <= 0) {
			break;
		}

		if (FD_ISSET (insock, &c_iofds)) {
			if ((bytes = read (insock, buf, sizeof(buf))) <= 0)
				break;
			if (write (outsock, buf, bytes) != bytes)
				break;
			writehex (buf, bytes);
			bytes_out += bytes;
		}

		if (FD_ISSET (outsock, &c_iofds)) {
			if ((bytes = read (outsock, buf, sizeof(buf))) <= 0)
				break;
			if (write (insock, buf, bytes) != bytes) break;
			bytes_in += bytes;
		}
	}

	printf ("Leaving main copyloop\n");

	shutdown (insock,  0);
	shutdown (outsock, 0);
	close (insock);
	close (outsock);
    	printf ("copyloop - sockets shutdown and closed\n");
	end_time = (unsigned int) time(NULL);
    	printf ("copyloop - connect time: %8d seconds\n",
						end_time - start_time);
    	printf ("copyloop - transfer in:  %8ld bytes\n", bytes_in);
    	printf ("copyloop - transfer out: %8ld bytes\n", bytes_out);
}

int writehex (const char *buf, const int bytes) {
	int	i;

	printf ("Transfer: %d bytes: [", bytes);
	for (i = 0; i < bytes; i++) {
		if (buf[i] >= ' ' && buf[i] < 127) {
			printf ("%c", buf[i]);
		} else {
			printf ("<0x%02x>", (unsigned char) (buf[i]));
		}
	}
	printf ("]\n");
	return i;
}
