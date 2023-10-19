#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define ESC    '\x1B'
#define LF     '\xA'
#define FF     '\xC'
#define FS     '\x1C'

static int    spacing    = 60;
static int    pos        = 0;
static int    page       = 0;
static int    pagelength = 11 * 360;
//static int    reallength = 10 * 360 + 240;
static int    reallength = 11 * 360;
static int    debug      = 0;

int main(int argc, char *argv[])
{
    int   i, c, n, m, n1, n2;
    int   errflag = 0;
    FILE  *fd = stdin;
    char  *prog, *ptr;

    if ((ptr = strrchr(argv[0], '/')) == NULL) {
        ptr = argv[0];
    } else {
        ptr++;
    }
    prog = ptr;

    while ((c = getopt(argc, argv, "bdvf:u:r:")) != EOF) {
        switch (c) {
        case 'd':
            debug   = 1;
            break;
        default:
            errflag++;
        }
    }

    if (errflag) {
        printf("usage: %s [-d] [file]\n", prog);
        return 1;
    }

    if (optind < argc) {
        if ((fd = fopen(argv[optind], "rb")) == NULL) {
            printf("-1\n");
            return -1;
        }
    }

    while ((c = getc(fd)) != EOF) {
        switch (c) {
        case ESC :
            switch(getc(fd)) {
            case '$' :
            case '\\':
                getc(fd);
            case 'l' :
            case 'R' :
            case 'Q' :
            case ' ' :
                getc(fd);
                break;
            case 'D' :
                for (i = 0; i < 32; i++)
                    if (getc(fd) == 0)
                        break;
                break;
            case '@' : /* reset printer */
                break;
            case 'J' : /* ESC J n,       advance n/180 inch */
                pos += getc(fd) * 2;
                if (pos >= reallength) {
                    if (debug)
                        fprintf(stderr, "across page boundary\n");
                    pos = 0;
                    page++;
                }
                break;
            case 'C' : /* ESC C n,       pagelength = n line */
                       /* ESC C NUL n,   pagelength = n inch */
                if ((n = getc(fd)) != 0) {
                    pagelength = n * spacing;
                } else {
                    pagelength = getc(fd) * 360;
                }
                if (debug)
                    fprintf(stderr, "Pagelength = %d\n", pagelength);
                break;
            case 'N' : /* ESC N n    */
                break;
            case 'O' : /* ESC O      */
                break;
            case '0' : /* ESC 0     1/8  */
                spacing = 45;
                if (debug)
                    fprintf(stderr, "Spacing = %d\n", spacing);
                break;
            case '2' : /* ESC 2     1/6 [default] */
                spacing = 60;
                if (debug)
                    fprintf(stderr, "Spacing = %d\n", spacing);
                break;
            case '3' : /* ESC 3 n   n/180 */
                spacing = getc(fd) * 2;
                if (debug)
                    fprintf(stderr, "Spacing = %d\n", spacing);
                break;
            case 'A' : /* ESC A n   n/60  */
                spacing = getc(fd) * 6;
                if (debug)
                    fprintf(stderr, "Spacing = %d\n", spacing);
                break;
            case '+' : /* ESC + n   n/360 */
                spacing = getc(fd);
                if (debug)
                    fprintf(stderr, "Spacing = %d\n", spacing);
                break;
            case 'B' : /* ESC B n1 n2 ... 0  max=16  */
                break;
            case '*' :
                m = getc(fd);
                n1 = getc(fd);
                n2 = getc(fd);
                m = (m >= 32) ? 3 : 1;
                for (i = 0; i < (m * (n1 + n2 * 256)); i++)
                    getc(fd);
                break;
            case 'Y' :
            case 'Z' :
            case 'K' :
            case 'L' :
                n1 = getc(fd);
                n2 = getc(fd);
                for (i = 0; i < (n1 + n2 * 256); i++)
                    getc(fd);
                break;
            }
            break;
        case FF  :
            pos = 0;
            page++;
            if (debug)
                fprintf(stderr, "form feed\n");
            break;
        case LF  :
            pos += spacing;
            if (pos >= reallength) {
                if (debug)
                    fprintf(stderr, "across page boundary\n");
                pos = 0;
                page++;
            }
            break;
        case FS  :
            switch(getc(fd)) {
            case 'D':
            case 'S':
            case 'T':
                getc(fd);
            case 'x':
            case '-':
                getc(fd);
                break;
            }
        default  :
            break;
        }
    }

    if (pos > 1)
        page++;

    if (debug)
        fprintf(stderr, "Page=%d, pos=%d, spacing=%d\n", page, pos, spacing);

    printf("%d\n", page);


    if (argc > 1);
        fclose(fd);

    return 0;
}
