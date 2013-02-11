bin_PROGRAMS = lanco
dist_man_MANS = lanco.8

lanco_SOURCES = log.c log.h lanco.h lanco.c \
	cgroups.c utils.c \
	init.c run.c release.c stop.c check.c ls.c top.c dump.c
lanco_LDFLAGS = -lrt @CURSES_LIBS@ @JANSSON_LIBS@
lanco_CFLAGS  = @CURSES_CFLAGS@ @JANSSON_CFLAGS@
