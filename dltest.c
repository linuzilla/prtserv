#include <stdio.h>
#include <dlfcn.h>


int main (int argc, char *argv[]) {
	void		*libp;
	const char	*err;
	int		(*pagecnt)(const char *fname);
	int		rc;

	if (! (libp = dlopen (argv[1], RTLD_LAZY))) {
		fprintf (stderr, "dlopen(): %s\n", dlerror ());
		exit (1);
	}

	pagecnt = dlsym (libp, "countpage");

	if ((err = dlerror ()) != NULL) {
		fprintf (stderr, "dlsym: %s\n", err);
		exit (1);
	}

	rc = pagecnt (argv[2]);

	fprintf (stderr, "%s: %d\n", argv[2], rc);

	dlclose (libp);
	return 0;
}
