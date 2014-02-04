bin_PROGRAMS = lancxo
dist_man_MANS = lancxo.8

lancxo_SOURCES = log.c log.h lancxo.h lancxo.c \
	cgroups.c utils.c \
	init.c run.c release.c stop.c check.c ls.c top.c dump.c
lancxo_LDFLAGS = -lrt @CURSES_LIBS@ @JANSSON_LIBS@
lancxo_CFLAGS  = @CURSES_CFLAGS@ @JANSSON_CFLAGS@
