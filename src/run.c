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

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

extern const char *__progname;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s <namespace> run [OPTIONS ...] task command\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "-f         run in foreground.\n");
	fprintf(stderr, "-L         force logging to a logfile.\n");
	fprintf(stderr, "-l logfile log output to the following file.\n");
	fprintf(stderr, "-c command execute a command when the task exits.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lancxo(8) for more information\n");
}

/**
 * Register a command to be run when the task exits. Only one such command can
 * be registered. This is done by creating a file
 * /var/run/lancxo-XXXX/task-exit-XXXXX containing the command. The release agent
 * will execute it.
 *
 * @param namespace Namespace.
 * @param task      Task name.
 * @param command   Command to execute or NULL if no command
 * @return 0 on success, -1 on error.
 */
static int
register_command(const char *namespace, const char *task, const char *command)
{
	char *path = NULL;
	if (asprintf(&path, RUNPREFIX "/lancxo-%s/task-exit-%s",
		namespace, task) == -1) {
		log_warn("run", "unable to allocate memory for new command");
		return -1;
	}
	if (unlink(path) == -1 && errno != ENOENT) {
		log_warn("run", "cannot remove old command file %s",
		    path);
		free(path);
		return -1;
	}

	if (command) {
		log_debug("run", "register new command for task %s", task);
		FILE *fcommand = fopen(path, "w");
		if (fcommand == NULL) {
			log_warn("run", "unable to create file %s", path);
			free(path);
			return -1;
		}
		free(path);
		fprintf(fcommand, "%s", command);
		fclose(fcommand);
	}
	return 0;
}

int
cmd_run(const char *namespace, int argc, char * const argv[])
{
	int ch;
	int background = 1;
	char *logfile = NULL;
	char *command = NULL;

	while ((ch = getopt(argc, argv, "hLl:fc:")) != -1) {
		switch (ch) {
		case 'h':
			usage();
			return 0;
		case 'f':
			background = 0;
			break;
		case 'L':
			logfile = "";
			break;
		case 'l':
			logfile = optarg;
			break;
		case 'c':
			command = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	/* task and command */
	if (optind > argc - 2) {
		usage();
		return -1;
	}

	const char *task = argv[optind++];
	if (!utils_is_valid_name(task)) {
		log_warnx("run", "task should be an alphanumeric ASCII string");
		return -1;
	}

	argc -= optind;
	argv = &argv[optind];

	log_debug("run", "check if the target cgroup exists");
	if (!cg_exist_named_hierarchy(namespace)) {
		log_warnx("run", "namespace %s should be created with init command",
			namespace);
		return -1;
	}
	if (cg_exist_task(namespace, task, NULL)) {
		log_warnx("run", "task %s is already running", task);
		return -1;
	}

	log_debug("run", "creating sub-cgroup for task %s", task);
	if (cg_create_task(namespace, task)) {
		log_warnx("run", "unable to create sub-cgroup for task %s", task);
		return -1;
	}

	if (register_command(namespace, task, command) == -1) {
		log_warnx("run", "unable to register command for task %s", task);
		return -1;
	}

	/* Redirect output */
	if (logfile && strlen(logfile)) logfile = strdup(logfile);
	if (((!logfile && background) ||
		(logfile && strlen(logfile) == 0)) &&
	    asprintf(&logfile, LOGPREFIX "/lancxo-%s/task-%s.log",
		namespace, task) == -1) {
		log_warn("run", "unable to allocate memory for logfile");
		return -1;
	}
	if (logfile) {
		log_debug("run", "redirect output to %s", logfile);
		if (utils_redirect_output(logfile) == -1) {
			log_warnx("run", "unable to redirect output to %s",
			    logfile);
			free(logfile);
			return -1;
		}
		free(logfile);
	}

	if (background && daemon(1, logfile?1:0) == -1) {
		log_warn("run", "unable to daemonize");
		return -1;
	}
	log_info("run", "run %s", argv[0]);
	if (execvp(argv[0], &argv[0]) == -1) {
		log_warn("run", "unable to run %s", argv[0]);
		return -1;
	}

	return 0;
}
