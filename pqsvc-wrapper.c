#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#define UNAME  "liujc"
#define SHELL  "/bin/sh"

/* #define DEBUG */

int main(int argc, char *argv[])
{
    struct passwd  *pwd, *upwd;
    int            uid = 0, gid = 0, login = 0;
    char           shell[100], loginshell[100];
    char           user[100], homepath[100];

    login =  (argc >= 2) && (strcmp(argv[1], "-") == 0) ? 1 : 0;

    if ((argc > 3) || ((argc == 3) && ! login)) {
        printf("usage: %s [-] [username]\n", argv[0]);
        exit(1);
    }

    strncpy(user, (argc == login + 2) ? argv[login+1] : "root", 99);

    if ((upwd = getpwnam(user)) == NULL) {
        printf("Can't find user \"%s\"\n", user);
        exit(1);
    }

    uid = upwd->pw_uid;
    gid = upwd->pw_gid;
    strcpy(loginshell, (upwd->pw_shell[0] != '\0') ? upwd->pw_shell : SHELL);
    strcpy(homepath, (upwd->pw_dir[0] != '\0') ? upwd->pw_dir : "/" );

    if ((pwd = getpwnam(UNAME)) == NULL) {
        printf("Segmentation Fault\n");
        exit(-1);
    }

    if (pwd->pw_uid != getuid()) {
        printf("permission deny!\n");
        exit(1);
    }

    if (login) {
        char   *ptr;

        if ((ptr = strrchr(loginshell, '/')) != NULL) {
            ptr++;
        } else {
            ptr = loginshell;
        }
        shell[0] = '-';
        strcpy(&shell[1], ptr);
    } else {

        if (getenv("SHELL") != NULL) {
            strcpy(loginshell, getenv("SHELL"));
        } else {
            strcpy(loginshell, (pwd->pw_shell[0] != '\0') ? pwd->pw_shell : SHELL);
        }
        strcpy(shell, loginshell);
    }

    setgid(gid);

    if (login) {
/*      setsid();         */
        setreuid(uid, uid);
    } else {
        setuid(uid);
    }

    if (gid != getgid()) {
        printf("can't setgid to %d\n", gid);
/*      exit(1); */
    }

    if (uid != getuid()) {
        printf("can't setuid to %d\n", uid);
        exit(1);
    }


    if (login) {
        char    *envp[10];
        char    buffer[256];
        int     i = 0;
        
        envp[i++] = strdup("PATH=/usr/bin:/usr/ucb:/usr/sbin");
        sprintf(buffer, "USER=%s",           user); envp[i++] = strdup(buffer);
        sprintf(buffer, "LOGNAME=%s",        user); envp[i++] = strdup(buffer);
//      sprintf(buffer, "MAIL=/var/mail/%s", user); envp[i++] = strdup(buffer);
        sprintf(buffer, "SHELL=%s",    loginshell); envp[i++] = strdup(buffer);
        sprintf(buffer, "HOME=%s",       homepath); envp[i++] = strdup(buffer);
        sprintf(buffer, "TERM=%s", getenv("TERM")); envp[i++] = strdup(buffer);
        envp[i] = NULL;

//      for (i = 0; envp[i] != NULL; i++) printf("%s\n", envp[i]);

        printf("Change directory to [%s]\n", homepath);
        chdir(homepath);
        printf("Running shell as [%s]\n", loginshell);

        execle(loginshell, shell, NULL, envp);
    } else {
        execl(loginshell, shell, NULL);
    }
    printf("Can't exec %s\n", loginshell);
}
