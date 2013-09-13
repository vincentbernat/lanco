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
	fprintf(stderr, "Usage: %s <namespace> stop task\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lancxo(8) for more information\n");
}

struct sequence {
	int signal;		/* Signal to send */
	int wait;		/* How many time to wait for */
};

/**
 * Signals to stop a task.
 */
static struct sequence stop[] = {
	{ SIGTERM, 20 },
	{ SIGTERM, 10 },
	{ SIGKILL, 5 },
	{ 0 }
};

int
cmd_stop(const char *namespace, int argc, char * const argv[])
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
		log_warnx("stop", "task should be an alphanumeric ASCII string");
		return -1;
	}
	ino_t inode = 0;
	if (!cg_exist_task(namespace, task, &inode)) {
		log_warnx("stop", "task %s is not running", task);
		return -1;
	}

	int stopped = 0;
	for (struct sequence *i = stop; i->signal > 0; i++) {
		log_debug("stop", "send signal %d to task %s", i->signal, task);
		if (cg_kill_task(namespace, task, inode, i->signal) == -1) {
			log_warnx("stop", "unable to stop task %s", task);
			return -1;
		}

		int wait = i->wait;
		while (wait-- > 0) {
			sleep(1);
			if (!cg_exist_task(namespace, task, &inode)) {
				log_debug("stop", "task %s does not exist anymore",
				    task);
				stopped++;
				break;
			}
		}
		if (stopped) break;
	}
	if (!stopped) {
		log_warnx("stop", "unable to stop task %s", task);
		return -1;
	}

	log_info("stop", "task %s has been terminated successfully", task);
	return 0;
}
