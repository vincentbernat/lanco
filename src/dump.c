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
#include <unistd.h>
#include <jansson.h>

extern const char *__progname;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s <namespace> top\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lanco(8) for more information\n");
}


static int
one_pid(const char *namespace, const char *name, pid_t pid, void *arg)
{
	json_t *pids = arg;
	const char *command = utils_cmdline(pid);

	/* Append the new PID */
	if (json_array_append_new(pids, json_pack("{s:i,s:o}",
		    "pid", pid,
		    "cmdline", command?json_string(command):json_null())) == -1) {
		log_warnx("dump", "unable to record PID %d for task %s",
		    pid, name);
		return -1;
	}

	return 0;
}

static int
one_task(const char *namespace, const char *name, void *arg)
{
	json_t *tasks = arg;
	json_t *pids = json_pack("[]");
	if (cg_iterate_pids(namespace, name, one_pid, pids) == -1) {
		return -1;
	}
	json_t *result = json_pack("{s:i,s:o}",
	    "count", json_array_size(pids),
	    "processes", pids);
	uint64_t cpu = cg_cpu_usage(namespace, name);
	if (cpu)
		json_object_set_new(result, "cpu", json_integer(cpu));

	if (json_object_set_new(tasks, name, result) == -1) {
		log_warnx("dump", "unable to record task %s", name);
		return -1;
	}
	return 0;
}

int
cmd_dump(const char *namespace, int argc, char * const argv[])
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

	json_t *tasks = json_pack("{}");

	if (cg_iterate_tasks(namespace, one_task, tasks) == -1) {
		log_warnx("ls", "error while walking tasks");
		return -1;
	}

	uint64_t cpu = cg_cpu_usage(namespace, NULL);
	int nbcpus = sysconf(_SC_NPROCESSORS_ONLN);

	json_t *result = json_pack("{s:s,s:i,s:o}",
	    "namespace", namespace,
	    "count", json_object_size(tasks),
	    "tasks", tasks);
	if (cpu > 0)
		json_object_set_new(result, "cpu",
		    json_integer(cg_cpu_usage(namespace, NULL)));
	if (nbcpus > 0)
		json_object_set_new(result, "nbcpus",
		    json_integer(nbcpus));

	json_dumpf(result, stdout, JSON_INDENT(1));

	return 0;
}
