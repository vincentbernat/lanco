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

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>

extern const char *__progname;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [OPTIONS] <namespace> <command> [OPTIONS ...]\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "-d      Be more verbose.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lanco(8) for more information\n");
}

struct cmd {
	const char *name;
	int(*fn)(const char *, int argc, char * const *argv);
};
static struct cmd lanco_cmds[] = {
	{ "init",    cmd_init },
	{ "run",     cmd_run  },
	{ "stop",    cmd_stop },
	{ "check",   cmd_check },
	{ "release", cmd_release },
	{ "ls",      cmd_ls },
	{ NULL }
};

/**
 * Expand argv[0] into arguments.
 *
 * lanco can be called with "lanco" but also with embedded arguments,
 * like this:
 *
 * @verbatim
 * lanco@@namespace@@init
 * lanco@@namespace@@run@@task@@sleep 10
 * @endverbatim
 *
 * We expand this here. Other arguments are pushed away.
 */
static void
expand(int *argc, char **argv[])
{
	int count = 0;
	char *ch = (*argv)[0], *pch;
	while (ch && *ch) {
		while (ch && *ch && *(ch+1) != '@') {
			ch++;
			ch = strchr(ch+1, '@');
		}
		if (ch && *ch) {
			count++;
			ch += 2;
		}
	}
	if (count == 0) return;

	/* We need to reallocate a new argv array (NULL terminated, like the
	 * original one) */
	char **nargv = calloc(count + *argc + 1, sizeof(char*));
	assert(nargv);

	/* New arguments */
	count = 0;
	ch = pch = (*argv)[0];
	while (ch && *ch) {
		while (ch && *ch && *(ch+1) != '@') {
			ch++;
			ch = strchr(ch, '@');
		}
		if (ch && *ch) {
			*ch = '\0';
			ch += 2;
			nargv[count++] = pch;
			pch = ch;
		}
	}
	nargv[count] = pch;

	/* Remaining arguments */
	for (int i = 1; i < *argc; i++) {
		nargv[count+i] = (*argv)[i];
	}
	*argc += count;
	*argv = nargv;
}

int
main(int argc, char * const argv[])
{
	int debug = 1;
	int ch;

	/* Do argv[0] expansion */
	expand(&argc, (char***)&argv);

	while ((ch = getopt(argc, argv, "+hvdD:")) != -1) {
		switch (ch) {
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		case 'v':
			fprintf(stdout, "%s\n", PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
			break;
		case 'd':
			debug++;
			break;
		case 'D':
			log_accept(optarg);
			break;
		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}

	log_init(debug, __progname);

	/* Namespace and command */
	if (optind > argc - 2) {
		usage();
		exit(EXIT_FAILURE);
	}

	const char *namespace = argv[optind];
	const char *command = argv[optind+1];

	if (!utils_is_valid_name(namespace)) {
		log_warnx("main", "namespace should be alphanumeric ASCII string");
		exit(EXIT_FAILURE);
	}

	log_debug("main", "namespace: %s", namespace);
	log_debug("main", "command: %s", command);
	argc -= optind;
	argv = &argv[optind];
	optind = 1;

	for (struct cmd *cmd = lanco_cmds; cmd->name; cmd++)
		if (!strcmp(cmd->name, command))
			return (cmd->fn(namespace,
				argc - optind,
				&argv[optind]) == 0)?EXIT_SUCCESS:EXIT_FAILURE;

	log_warnx("main", "no command `%s`", command);
	usage();
	exit(EXIT_FAILURE);
}
