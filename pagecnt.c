#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <regex.h>

#include "epson_page.c"

static int pcl_page (const char *fname) {
	int	fd;
	int	i, len, rc = 0;
	char	buffer[4096];
	int	state = 0;

	if ((fd = open (fname, O_RDONLY)) < 0) return -1;

	while ((len = read (fd, buffer, sizeof buffer)) > 0) {
		for (i = 0; i < len; i++) {
			if (buffer[i] == 12) {
				state = 0;
				rc++;
			} else if (buffer[i] == '\x1a') {
				state = 1;
			} else if (state != 0) {
				if (buffer[i] == '&' && state == 1) {
					state = 2;
				} else if (buffer[i] == 'l' && state == 2) {
					state = 3;
				} else if (buffer[i] == '0' && state == 3) {
					state = 4;
				} else if (buffer[i] == 'H' && state == 4) {
					state = 0;
					rc++;
				}
			} else {
				state = 0;
			}
		}
	}

	close (fd);

	return -1;
	// return rc;
}

static int pdf_page (const char *fname) {
	return -1;
}

static int ps_page (FILE *fp) {
	char		buffer[4096];
	int		rc = 0;
	regex_t		preg;
	regmatch_t	pmatch[2];
	regex_t		preg2;
	regmatch_t	pmatch2[2];
	regex_t		preg3;
	regmatch_t	pmatch3[2];
	char		*nc = "%%BeginNonPPDFeature: NumCopies ";
	int		nclen;
	int		cnt = 0, v;
	int		cnt2 = 0;
	int		numcp = 1;

	nclen = strlen (nc);

	if (regcomp (&preg, "^\\(%%\\[Page: *([0-9]+)\\]%%\\)",
					REG_EXTENDED|REG_NEWLINE) != 0) {
		fprintf (stderr, "regcomp: compiling error");
		return -1;
	}

	if (regcomp (&preg2, "^%%Page: *([0-9]+) *",
					REG_EXTENDED|REG_NEWLINE) != 0) {
		fprintf (stderr, "regcomp: compiling error");
		return -1;
	}

	if (regcomp (&preg3, "^%%Requirements: numcopies\\(([0-9]+)\\)",
					REG_EXTENDED|REG_NEWLINE) != 0) {
		fprintf (stderr, "regcomp: compiling error");
		return -1;
	}


	while (fgets (buffer, sizeof buffer -1, fp) != NULL) {
		if (regexec (&preg, buffer, 2, pmatch, 0) == 0) {
			buffer[pmatch[1].rm_eo] = '\0';
			v = atoi (&buffer[pmatch[1].rm_so]);
			if (v > rc) rc = v;
			++cnt;
		} else if (regexec (&preg2, buffer, 2, pmatch2, 0) == 0) {
			buffer[pmatch2[1].rm_eo] = '\0';
			++cnt2;
			/*
			v = atoi (&buffer[pmatch2[1].rm_so]);
			if (v > rc) rc = v;
			++cnt;
			*/
			if (cnt2 > rc) rc = cnt2;
		} else if (regexec (&preg3, buffer, 2, pmatch3, 0) == 0) {
			buffer[pmatch3[1].rm_eo] = '\0';
			v = atoi (&buffer[pmatch3[1].rm_so]);
			if (v > numcp) numcp= v;
		} else if (strncmp (buffer, nc, nclen) == 0) {
			numcp = atoi (&buffer[nclen]);
		}
	}

	return rc * numcp;
}

int countpage (const char *fname) {
	FILE	*fp;
	char	buffer[1024];
	int	line = 0;
	char	pjlstart[]  = "\x1b%-12345X@PJL ";
	char	ps_start[]  = "%!PS-Adobe-";
	char	pcl_start[] = ") HP-PCL XL";
	char	pdf_start[] = "%PDF-";
	char	pclx_start[] = "@PJL ENTER LANGUAGE=PCL";
	int	rc = 0;
	int	len;
	int	pslen, pcl_len, pdf_len, pclx_len;
	short	pjl = 0;
	short	pjtype = 0;	// UNKNOW, ESC/P, PCL, PostScript

	if ((fp = fopen (fname, "r")) == NULL) {
		perror (fname);
		return -1;
	}

	pslen    = strlen (ps_start);
	pcl_len  = strlen (pcl_start);
	pdf_len  = strlen (pdf_start);
	pclx_len = strlen (pclx_start);

	while (fgets (buffer, sizeof buffer -1, fp) != NULL) {
		if (++line == 1) {
			len = strlen (pjlstart);

			if (strncmp (buffer, pjlstart, len) == 0) {
				pjl = 1;
				continue;
			} else if (buffer[0] == '\x1b') {
				pjtype = 1;	// ESC
				break;
			}
		}

		if (strncmp (buffer, pdf_start, pdf_len) == 0) {
			pjtype = 4;
			break;
		} else if (strncmp (buffer, ps_start, pslen) == 0) {
			pjtype = 3;
			break;
		} else if (strncmp (buffer, pcl_start, pcl_len) == 0) {
			pjtype = 2;
			break;
		} else if (strncmp (buffer, pclx_start, pclx_len) == 0) {
			pjtype = 2;
			break;
		} else if (strncmp (buffer, "@PJL ", 4) == 0) {
			if (! pjl) {
				pjtype = 1;
				break;
			}
		} else {
			pjtype = 1;
			break;
		}
	}

	switch (pjtype) {
	case 1:	// ESC/P
		rc = epson_page (fname);
#ifdef STANDALONE
		fprintf (stderr, "ESC/P [%s] Page=%d\n", fname, rc);
#endif
		break;
	case 2: // PCL
		rc = pcl_page (fname);
#ifdef STANDALONE
		fprintf (stderr, "PCL   [%s] Page=%d\n", fname, rc);
#endif
		break;
	case 3: // PostScript
		rc = ps_page (fp);
#ifdef STANDALONE
		fprintf (stderr, "PS    [%s] Page=%d\n", fname, rc);
#endif
		break;
	case 4: // PDF
		rc = pdf_page (fname);
#ifdef STANDALONE
		fprintf (stderr, "PDF   [%s] Page=%d\n", fname, rc);
#endif
		break;
	default:
		break;
	}
	fclose (fp);

	return rc;
}

#ifdef STANDALONE

int main (int argc, char *argv[]) {
	int	i;

	for (i = 1; i < argc; i++) {
		countpage (argv[i]);
	}

	return 0;
}

#endif
