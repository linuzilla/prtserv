#
#
#MYSQL_DIR=/usr/local/misc/mysql-4.0.13
NETSNMP_DIR=/usr/local/misc/net-snmp-5.1.2
MYSQL_DIR=/usr/local/misc/mysql-4.0.21
SYSTEM_OS:= $(shell uname)

ifeq ($(SYSTEM_OS),Linux)
	MYSQL_DIR=/usr
endif

CC	= gcc
CCOPT	= -Wall -O2 -g
# CCOPT	= -Wall -O2
# INCLS	= -I. @V_INCLS@ -static
#INCLS   =  -DDEBUG_LEVEL=2
#INCLS   = -DYYDEBUG=1 -DDEBUG_LEVEL=1 -DHAVE_CONFIG_H=1
#DEFS   += -DHAVE_CONFIG_H=1
#DEFS    = -DUSE_PROCESSOR_TIME
#LOPT    = -pthread

DEFS   = -I$(MYSQL_DIR)/include
DEFS   += -L$(MYSQL_DIR)/lib/mysql
DEFS   += -I$(NETSNMP_DIR)/include
DEFS   += -L$(NETSNMP_DIR)/lib
LOPT   += -lmysqlclient
LOPT   += -lnetsnmphelpers -lnetsnmpmibs -lnetsnmpagent -lnetsnmp -lcrypto
LOPT   += -lwrap -lkvm
# LOPT   += -ltds -lsybdb
LOPT   += -lbz2

# ifeq ($(SYSTEM_OS),SunOS)
ifeq ($(SYSTEM_OS),FreeBSD)
	# DEFS   += -R$(FREETDS_DIR)/lib
	DEFS   += -R$(MYSQL_DIR)/lib/mysql
	DEFS   += -R$(NETSNMP_DIR)/lib
endif

CFLAGS = $(CCOPT) $(INCLS) $(DEFS) $(OSDEPOPT)

SRC =  lpcloned.c \
       x_object.c db_mysql.c autofree.c sys_conf.c base64.c xtimer.c

SPSSRC = sendps.c x_object.c db_mysql.c autofree.c sys_conf.c snmp_lib.c \
	 xtimer.c

PQSRC = pqsvc.c x_object.c db_mysql.c autofree.c sys_conf.c

LPRSRC = dlpr.c pagecnt.c base64.c xtimer.c \
	 sys_conf.c x_object.c db_mysql.c autofree.c

PDWSRC = pdfwrite.c x_object.c db_mysql.c autofree.c sys_conf.c xtimer.c

OBJ    = $(SRC:.c=.o)
SPSOBJ = $(SPSSRC:.c=.o)
PQOBJ  = $(PQSRC:.c=.o)
LPROBJ = $(LPRSRC:.c=.o)
PDWOBJ = $(PDWSRC:.c=.o)

VER := $(shell sed -e 's/.*\"\(.*\)\"/\1/' VERSION)

GCCVER := $(shell gcc -v 2>&1 | grep "gcc version" | awk '{print $$3}')
OSREL  := $(shell uname -r | sed 's/\([.0-9]*\).*/\1/')
# CFLAGS += -DGCC_VERSION=\"$(GCCVER)\" -DOS_RELEASE=\"$(OSREL)\"
CFLAGS += -DVERSION=\"$(VER)\"
TARGET = lpcloned sendps jetdirect pqsvc countepson dlpr \
	 pdfwrite lpr_print pagecnt libpagecnt.so

LEXYACCTMP = lex.yy.c y.tab.c y.tab.h y.output y.tab.o lex.yy.o
CLEANFILES = $(OBJ) $(SPSOBJ) $(PQOBJ) $(LPROBJ) $(TARGET) $(LEXYACCTMP)

.c.o:
	@rm -f $@
	$(CC) $(CFLAGS) -c $*.c

all: $(TARGET)

lpcloned:	$(OBJ) lex.yy.o y.tab.o
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(OBJ) lex.yy.o y.tab.o $(LOPT)

pagecnt:	pagecnt.c
	@rm -f $@
	$(CC) $(CFLAGS) -DSTANDALONE -o $@ $<

sendps:		sendps.c
	@rm -f $@
	$(CC) $(CFLAGS) -DENABLE_MYSQL_SUPPORT=0 -o $@ $<

jetdirect:	$(SPSOBJ) lex.yy.o y.tab.o
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(SPSOBJ) lex.yy.o y.tab.o $(LOPT)

pdfwrite:	$(PDWOBJ) lex.yy.o y.tab.o
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(PDWOBJ) lex.yy.o y.tab.o $(LOPT)

pqsvc:	$(PQOBJ) lex.yy.o y.tab.o
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(PQOBJ) lex.yy.o y.tab.o $(LOPT)

countepson:	countepson.c
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $<

dlpr:	dlpr.c xtimer.o
	@rm -f $@
	$(CC) $(CFLAGS) -DENABLE_MYSQL_SUPPORT=0 -o $@ dlpr.c xtimer.o

lpr_print:	$(LPROBJ) lex.yy.o y.tab.o
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(LPROBJ) lex.yy.o y.tab.o $(LOPT)

libpagecnt.so:	pagecnt.c
	@rm -f $@ pagecnt.o
	$(CC) $(CFLAGS) -c -fPIC $<
	$(CC) -shared -o $@ pagecnt.o

install:
	install -m 755 -o bin -g bin -s crsdisp /usr/local/bin
	install -m 644 -o bin -g bin crsdisp.conf-sample /usr/local/etc


y.tab.c:        parser.y
	bison -v -t -d -y parser.y

lex.yy.c:       lexer.l y.tab.c
	flex lexer.l

clean:
	rm -f $(CLEANFILES) sendps.o
