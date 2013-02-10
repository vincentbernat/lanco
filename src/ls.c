/* -*- mode: c; c-file-style: "openbsd" -*- */
/*
 * Copyright (c) 2013 Vincent Bernat <vincent.bernat@dailymotion.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "lanco.h"

#include <unistd.h>
#include <getopt.h>
#include <string.h>

extern const char *__progname;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s <namespace> ls\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lanco(8) for more information\n");
}

static int
one_pid(const char *namespace, const char *task, pid_t pid)
{
	/* Retrieve command line from /proc/PID/cmdline */
	char *path = NULL;
	FILE *cmdline = NULL;
	char command[256] = {};

	if (asprintf(&path, "/proc/%d/cmdline", pid) == -1) return 0;
	cmdline = fopen(path, "r");
	if (cmdline == NULL) {
		/* Vanished? */
		free(path);
		return 0;
	}
	free(path);

	int nb = fread(command, 1, sizeof(command) - 1, cmdline);
	fclose(cmdline);
	if (nb <= 0) {
		strcpy(command, "????");
	} else {
		while (--nb >= 0) {
			if (command[nb] == '\0')
				command[nb] = ' ';
		}
	}

	fprintf(stdout, " │  → %5d %s\n", pid, command);
	return 0;
}

static int
one_task(const char *namespace, const char *task)
{
	fprintf(stdout, " ├ %s\n", task);
	return cg_iterate_pids(namespace, task, one_pid);
}

int
cmd_ls(const char *namespace, int argc, char * const argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch (ch) {
		case 'h':
			usage();
			return 0;
		default:
			usage();
			return -1;
		}
	}

	fprintf(stdout, "%s\n", namespace);
	if (cg_iterate_tasks(namespace, one_task) == -1) {
		log_warnx("ls", "error while walking tasks");
		return -1;
	}
	fprintf(stdout, " ╯\n");
	return 0;
}
