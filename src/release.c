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

#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

extern const char *__progname;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s <namespace> release [task]\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lanco(8) for more information\n");
}

static void
execute_hook(const char *namespace, const char *task)
{
	char *path = NULL;
	if (asprintf(&path, RUNPREFIX "/lanco-%s/task-exit-%s",
		namespace, task) == -1) {
		log_warn("release", "unable to allocate memory for executing hook");
		return;
	}

	struct stat a;
	if (stat(path, &a) == -1) {
		log_debug("release", "no command to execute");
		free(path);
		return;
	}

	log_debug("release", "change UID/GID to match saved ones");
	if (setresgid(a.st_uid, a.st_uid, a.st_uid) == -1 ||
	    setresuid(a.st_gid, a.st_gid, a.st_gid) == -1) {
		log_warn("release", "unable to change UID/GID to %d:%d",
		    a.st_uid, a.st_gid);
		log_warn("release", "not executing command hook");
		free(path);
		return;
	}

	log_debug("release", "execute with a shell the provided command");
	if (execl("/bin/sh", "sh", path, NULL) == -1) {
		log_warn("release", "unable to execute the provided command");
		free(path);
		return;
	}
	/* Shouldn't be here */
}

int
cmd_release(const char *namespace, int argc, char * const argv[])
{
	int ch;
	int dryrun = 0;

	while ((ch = getopt(argc, argv, "hn")) != -1) {
		switch (ch) {
		case 'h':
			usage();
			return 0;
		case 'n':
			dryrun = 1;
			break;
		default:
			usage();
			return -1;
		}
	}

	/* task */
	if (optind < argc) {
		const char *task = argv[optind++];
		if (!strcmp(task, "/")) {
			log_debug("release", "ask for release of namespace, ignore");
			return 0;
		}
		if (!strncmp(task, "/task-", strlen("/task-"))) {
			log_debug("release", "release agent is asking to relese %s in %s",
			    task, namespace);
			task += strlen("/task-");
		}
		if (!utils_is_valid_name(task)) {
			log_warnx("release", "task should be an alphanumeric ASCII string");
			return -1;
		}
		if (cg_release_task(namespace, task) == -1) {
			log_warnx("release", "unable to release task %s", task);
			return -1;
		}
		log_info("release", "task %s in %s has been released", task, namespace);
		if (!dryrun) execute_hook(namespace, task);
		return 0;
	}

	/* namespace */
	if (cg_delete_hierarchies(namespace) == -1) {
		log_warnx("release", "unable to delete namespace %s", namespace);
		return -1;
	}
	log_info("release", "namespace %s has been destroyed", namespace);
	return 0;
}
