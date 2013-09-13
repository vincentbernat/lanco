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

#include "lancxo.h"

#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>

extern const char *__progname;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s <namespace> check task\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lancxo(8) for more information\n");
}

int
cmd_check(const char *namespace, int argc, char * const argv[])
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

	if (optind > argc - 1) {
		usage();
		return -1;
	}

	const char *task = argv[optind++];
	if (!utils_is_valid_name(task)) {
		log_warnx("check", "task should be an alphanumeric ASCII string");
		return -1;
	}
	if (!cg_exist_task(namespace, task, NULL)) {
		log_info("check", "task %s is not running", task);
		return -1;
	}
	log_info("check", "task %s is running", task);
	return 0;
}
