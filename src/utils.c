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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>

/**
 * Check if a path is a mount point.
 *
 * @param path   Path we want to check.
 * @param parent Parent of path.
 * @return 1 if the path is a mount point, 0 otherwise
 */
int
utils_is_mount_point(const char *path, const char *parent)
{
	struct stat a, b;
	if (stat(path, &a) == -1) {
		log_debug("utils", "unable to stat %s", path);
		return 0;
	}
	if (stat(parent, &b) == -1) {
		log_debug("utils", "unable to stat %s", parent);
		return 0;
	}

	return (a.st_dev != b.st_dev);
}

/**
 * Check if a path is a directory and is empty.
 *
 * @param path   Path we want to check.
 * @return 1 if the path is an empty directory, 0 otherwise.
 */
int
utils_is_empty_dir(const char *path)
{
	struct stat a;
	if (lstat(path, &a) == -1) {
		log_debug("utils", "unable to stat %s", path);
		return 0;
	}
	if (!S_ISDIR(a.st_mode)) {
		log_debug("utils", "%s not a directory", path);
		return 0;
	}

	int n = 0;
	DIR *dir = opendir(path);
	if (dir == NULL) {
		log_warn("utils", "cannot open directory %s", path);
		return 0;
	}
	errno = 0;
	while (readdir(dir) && errno == 0) n++;
	if (errno != 0) {
		log_warn("utils", "unable to read directory %s", path);
		closedir(dir);
		return 0;
	}
	closedir(dir);
	log_debug("utils", "directory %s has %d entries", path, n);
	return (n == 2);
}

/**
 * Check if a path is a directory and owned by a given uid/gid.
 *
 * @param path  Path we want to check.
 * @param uid   Owner UID or -1 to check if it is owned by the current UID.
 * @param gid   Owner GID or -1 to check if it is owned by the current GID.
 *
 * @return 1 if the path is a directory with correct owner or 0 otherwise.
 */
int
utils_is_dir_owned(const char *path, uid_t uid, gid_t gid)
{
	struct stat a;
	if (stat(path, &a) == -1) {
		log_debug("utils", "unable to stat %s", path);
		return 0;
	}
	if (!S_ISDIR(a.st_mode)) {
		log_debug("utils", "%s is not a directory", path);
		return 0;
	}

	if ((a.st_uid != ((uid == -1)?geteuid():uid)) ||
	    (a.st_gid != ((gid == -1)?getegid():gid))) {
		log_debug("utils", "%s is not owned by %d:%d", path, uid, gid);
		return 0;
	}

	return 1;
}

/**
 * Check if a name (namespace or task) is valid.
 *
 * Those names are used in paths. We want to ensure they don't have anything
 * special. Only alpha numeric ascii characters are recognized (plus hypens).
 *
 * @param name Name to check for.
 * @return 1 if the name is valid, 0 otherwse
 */
int
utils_is_valid_name(const char *name)
{
	while (name) {
		if (!((isalnum(*name) && isascii(*name)) ||
			(*name == '.') || (*name == '-') || (*name == '_')))
			break;
		name++;
	}
	return (*name == '\0');
}

/**
 * Create a subdirectory for a namespace
 *
 * @param prefix Prefix for the subdirectory
 * @param namespace Namespace
 * @return 0 if success or -1 otherwise
 *
 * The directory is prefix + "/lanco-" + namespace
 */
int
utils_create_subdirectory(const char *prefix, const char *namespace,
    uid_t uid, gid_t gid)
{
	char *dir = NULL;
	if (asprintf(&dir, "%s/lanco-%s", prefix, namespace) == -1) {
		log_warn("utils", "unable to format directory for %s",
		    namespace);
		return -1;
	}
	if (utils_is_dir_owned(dir, uid, gid))
		log_debug("utils", "directory %s already exist", dir);
	else {
		if (mkdir(dir, 0755) == -1) {
			log_warn("utils", "unable to setup directory %s",
			    dir);
			free(dir);
			return -1;
		}
		if (chown(dir, uid, gid) == -1) {
			log_warn("utils", "unable to set uid/gid %d/%d for %s",
			    uid, gid, dir);
			if (rmdir(dir) == -1)
				log_warn("utils", "additionally, unable to remove "
				    "directory %s", dir);
			free(dir);
			return -1;
		}
		log_debug("utils", "directory %s created", dir);
		free(dir);
	}
	return 0;
}

/**
 * Rotate a logfile.
 *
 * @param logfile Name of logfile
 * @return 0 on success, -1 otherwise
 *
 * logfile is renamed to logfile.1, logfile.1 is renamed to logfile.2, etc.
 */
static int
utils_rotate(const char *logfile)
{
	char *new = NULL, *old = NULL;
	int maxlen = 0;
	unsigned i = 0;
	if (asprintf(&new, "%s.%u", logfile, UINT_MAX) == -1 ||
	    (old = strdup(new)) == NULL) {
		log_warn("utils", "unable to get memory for file rotation");
		free(new);
		return -1;
	}

	maxlen = strlen(new);
	for (i = 0; i < UINT_MAX; i++) {
		struct stat a;
		snprintf(new, maxlen + 1, "%s.%u", logfile, i);
		if (stat(new, &a) == -1) break;
	}
	if (i == UINT_MAX) unlink(new); /* unlikely */

	log_debug("utils", "%s is free, start rotation", new);
	for (; i > 0; i--) {
		snprintf(old, maxlen + 1, "%s.%u", logfile, i-1);
		if (rename(old, new) == -1) {
			log_warn("utils", "unable to rotate %s", old);
			free(old); free(new);
			return -1;
		}
		strcpy(new, old);
	}
	free(old);
	if (rename(logfile, new) == -1) {
		log_warn("utils", "unable to rotate %s", logfile);
		free(new);
		return -1;
	}
	free(new);
	return 0;
}

/**
 * Redirect output to a logfile. Due rotation if the logfile already exists.
 *
 * @param logfile Name of logfile
 * @return 0 on success, -1 otherwise
 */
int
utils_redirect_output(const char *logfile)
{
	log_debug("utils", "check if %s exists", logfile);
	struct stat a;
	if (stat(logfile, &a) != -1) {
		log_debug("utils", "%s exists, do rotation", logfile);
		if (utils_rotate(logfile) == -1)
			return -1;
	}

	log_debug("utils", "open %s for logging", logfile);
	int fd = open(logfile, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (fd == -1) {
		log_warn("utils", "unable to open %s", logfile);
		return -1;
	}
	int devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		log_warn("utils", "unable to open /dev/null");
		close(fd);
		return -1;
	}
	dup2(devnull, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	if (fd > 2) close(fd);
	if (devnull > 2) close(devnull);
	return 0;
}

/**
 * Get command line for a given PID.
 *
 * @param pid PID to search for command line.
 * @return statically allocated string with command line or NULL if not found
 */
char *
utils_cmdline(pid_t pid)
{
	/* Retrieve command line from /proc/PID/cmdline */
	char *path = NULL;
	FILE *cmdline = NULL;
	static char command[256] = {};

	if (asprintf(&path, "/proc/%d/cmdline", pid) == -1) return 0;
	cmdline = fopen(path, "r");
	if (cmdline == NULL) {
		/* Vanished? */
		free(path);
		return NULL;
	}
	free(path);

	int nb = fread(command, 1, sizeof(command) - 1, cmdline);
	fclose(cmdline);
	if (nb <= 0) return NULL;
	command[nb] = '\0';
	while (--nb >= 0) {
		if (command[nb] == '\0')
			command[nb] = ' ';
	}
	return command;

}
