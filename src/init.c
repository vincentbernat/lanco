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
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

extern const char *__progname;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s <namespace> init [OPTIONS ...]\n",
		__progname);
	fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
	fprintf(stderr, "\n");
	fprintf(stderr, "-u USER   user allowed to use the namespace.\n");
	fprintf(stderr, "-g GROUP  group allowed to use the namespace.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "see manual page lancxo(8) for more information\n");
}

int
cmd_init(const char *namespace, int argc, char * const argv[])
{
	int ch;
        const char *user = NULL;
        const char *group = NULL;
	uid_t uid = -1;
	gid_t gid = -1;

	while ((ch = getopt(argc, argv, "hu:g:")) != -1) {
		switch (ch) {
		case 'h':
			usage();
			return 0;
		case 'u':
			user = optarg;
			break;
		case 'g':
			group = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	if (user) {
		struct passwd *pw = getpwnam(user);
		if (pw == NULL) {
			log_warn("init", "unable to find user %s", user);
			return -1;
		}
		uid = pw->pw_uid;
		log_debug("init", "namespace will be owned by user %s (%d)",
		    user, uid);
	}
	if (group) {
		struct group *gr = getgrnam(group);
		if (gr == NULL) {
			log_warn("init", "unable to find gropup %s", group);
			return -1;
		}
		gid = gr->gr_gid;
		log_debug("init", "namespace will be owned by group %s (%d)",
		    group, gid);
	}

	/* Default log directory */
	log_debug("init", "creating directory to log tasks of %s", namespace);
	if (utils_create_subdirectory(LOGPREFIX, namespace, uid, gid) == -1) {
		log_warnx("init", "unable to create log directory for %s",
		    namespace);
		return -1;
	}
	/* Default run directory */
	log_debug("init", "creating directory for tun tasks of %s", namespace);
	if (utils_create_subdirectory(RUNPREFIX, namespace, uid, gid) == -1) {
		log_warnx("init", "unable to create run directory for %s",
		    namespace);
		return -1;
	}

	/* Initial cgroup */
	log_debug("init", "creating cgroup for %s", namespace);
	if (cg_setup_hierarchies(namespace, uid, gid) == -1)
		return -1;

	log_info("init", "namespace %s has been created",
	    namespace);

	return 0;
}
