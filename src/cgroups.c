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
#include <sys/mount.h>
#include <sys/queue.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>

/**
 * Set permissions on a given cgroup.
 *
 * @param path Path to cgroup
 * @param uid  UID to use or -1 for no change
 * @param gid  GID to use or -1 for no change
 * @return 0 on success, -1 on error.
 */
static int
cg_fix_permissions(const char *path, uid_t uid, gid_t gid)
{
	if (uid == -1 && gid == -1)
		return 0;
	if (chown(path, uid, gid) == -1)
		return -1;

	char *tasks = NULL;
	if (asprintf(&tasks, "%s/tasks", path) == -1)
		return -1;
	if (chown(tasks, uid, gid) == -1) {
		free(tasks);
		return -1;
	}
	free(tasks);
	return 0;
}

/**
 * Release a task in the given namespace. Should be empty
 *
 * @param root      Root hierarchy to use.
 * @param namespace Namespace we need to find the task.
 * @param task      Task name.
 * @param log       Log function to use for logging errors.
 * @return 0 on success and -1 on error
 */
static int
_cg_release_task(const char *root, const char *namespace, const char *task,
    void(*log)(const char *, const char *, ...))
{
	char *path = NULL;
	if (asprintf(&path, "%s/lanco-%s/task-%s",
		root, namespace, task) == -1) {
		log("cgroups", "unable to allocate memory to check for task");
		return -1;
	}
	if (rmdir(path) == -1) {
		log("cgroups", "unable to remove directory %s", path);
		free(path);
		return -1;
	}
	return 0;
}

/**
 * Release a task in the given namespace. Nobody should be in the task.
 *
 * @param namespace Namespace we need to find the task.
 * @param task      Task name.
 * @return 0 on success, -1 on error.
 */
int
cg_release_task(const char *namespace, const char *task)
{
	if (_cg_release_task(CGCPUACCT, namespace, task, log_debug) == -1) {
		log_info("cgroups", "unable to release task to a cpuacct cgroup");
		log_info("cgroups", "no future CPU accounting for task %s", task);
	}
	if (_cg_release_task(CGROOT, namespace, task, log_warn) == -1)
		return -1;
	return 0;
}

/**
 * Create a new task in the given namespace. Also move ourself in this task.
 *
 * @param root      Root hierarchy to use for the new task.
 * @param namespace Namespace we need to find the task.
 * @param task      Task name.
 * @param log       Log function to use for logging errors.
 * @return 0 on success and -1 on error
 */
static int
_cg_create_task(const char *root, const char *namespace, const char *task,
    void(*log)(const char *, const char *, ...))
{
	int rc = -1;
	char *path = NULL;
	char *tasks = NULL;
	if (asprintf(&path, "%s/lanco-%s/task-%s",
		root, namespace, task) == -1 ||
	    asprintf(&tasks, "%s/tasks", path) == -1) {
		log("cgroups", "unable to allocate memory to check for task");
		free(path);
		return -1;
	}
	if (mkdir(path, 0755) == -1) {
		log("cgroups", "unable to create directory %s", path);
		free(path);
		free(tasks);
		return -1;
	}

	/* Move ourself to the task */
	log_debug("cgroups", "move ourself into %s", path);
	FILE *taskfile = fopen(tasks, "w");
	if (taskfile == NULL) {
		log("cgroups", "unable to open tasks file in %s", path);
		goto end;
	}
	if (fprintf(taskfile, "%d", getpid()) < 0) {
		log("cgroups", "unable to move ourself in task %s", task);
		fclose(taskfile);
		goto end;
	}
	fclose(taskfile);

	rc = 0;
end:
	if (rc == -1 && rmdir(path) == -1)
		log("cgroups", "unable to remove task dir %s", path);
	free(path);
	free(tasks);
	return rc;
}

/**
 * Create a new task in the given namespace. Also move ourself in this task.
 *
 * @param namespace Namespace we need to find the task.
 * @param task      Task name.
 * @return 0 on success and -1 on error
 */
int
cg_create_task(const char *namespace, const char *task)
{
	if (_cg_create_task(CGROOT, namespace, task, log_warn) == -1)
		return -1;
	if (_cg_create_task(CGCPUACCT, namespace, task, log_debug) == -1) {
		log_info("cgroups", "unable to assign task to a cpuacct cgroup");
		log_info("cgroups", "no CPU accounting for task %s", task);
	}
	return 0;
}

/**
 * Check if a given task exist.
 *
 * @param namespace Namespace we need to find the task.
 * @param task      Task name.
 * @param inode When not NULL, store task inode if task is found. When not 0,
 *              check if this is the right inode.
 * @return 1 if the namespace exists, 0 otherwise
 */
int
cg_exist_task(const char *namespace, const char *task, ino_t *inode)
{
	char *path = NULL;
	if (asprintf(&path, "%s/lanco-%s/task-%s",
		CGROOT, namespace, task) == -1) {
		log_warn("cgroups", "unable to allocate memory to check for task");
		return 0;
	}

	struct stat a;
	if (stat(path, &a) == -1) {
		log_debug("cgroups", "task %s does not exist in namespace %s", task,
			namespace);
		free(path);
		return 0;
	}
	if (!S_ISDIR(a.st_mode)) {
		log_warnx("cgroups", "%s is not a directory", path);
		free(path);
		return 0;
	}
	free(path);
	if (inode && *inode > 0 && *inode != a.st_ino) {
		log_debug("cgroups", "task %s exists but not the right inode", task);
		return 0;
	}
	if (inode) *inode = a.st_ino;

	log_debug("cgroups", "task %s exists in namespace %s", task, namespace);
	return 1;
}

struct one_pid {
	TAILQ_ENTRY(one_pid) next;
	pid_t pid;
};
/**
 * Kill a task.
 *
 * To avoid race condition, the inode of the task directory, as provided by
 * cg_exist_task() should be provided. Killing is not recursive.
 *
 * @param namespace Namespace we need to find the task.
 * @param task      Task name.
 * @param inode     Task inode or 0 to not check it.
 * @param signal    Signal to send.
 * @return 0 on success and -1 on error
 */
int
cg_kill_task(const char *namespace, const char *task, ino_t inode, int signal)
{
	int rc = -1;
	char *dirpath = NULL;
	char *taskspath = NULL;
	DIR *dir = NULL;
	FILE *tasks = NULL;
	TAILQ_HEAD(, one_pid) pids;
	TAILQ_INIT(&pids);

	if (asprintf(&dirpath, "%s/lanco-%s/task-%s",
		CGROOT, namespace, task) == -1 ||
	    asprintf(&taskspath, "%s/tasks", dirpath) == -1) {
		log_warn("cgroups", "unable to allocate memory to check for task");
		rc = 0;
		goto end;
	}

	int done;
	do {
		done = 1;
		log_debug("cgroups", "locate tasks file in %s", dirpath);
		struct stat a;
		if (inode > 0) {
			if ((dir = opendir(dirpath)) == NULL) {
				if (errno == ENOENT) {
					log_debug("cgroups", "task %s has vanished", task);
					rc = 0;
					goto end;
				}
				log_warn("cgroups", "unable to open %s", dirpath);
				goto end;
			}

			/* Check inode */
			if (fstat(dirfd(dir), &a) == -1) {
				log_warn("cgroups", "unable to stat %s", dirpath);
				goto end;
			}
			if (a.st_ino != inode) {
				log_debug("cgroups", "task %s does not have the right inode",
				    task);
				rc = 0;
				goto end;
			}
			log_debug("cgroups", "task %s has the correct inode number",
			    task);
		}

		if ((tasks = fopen(taskspath, "r")) == NULL) {
			if (errno == ENOENT) {
				log_debug("cgroups", "task %s has vanished", task);
				rc = 0;
				goto end;
			}
			log_warn("cgroups", "unable to open %s", taskspath);
			goto end;
		}

		/* Check inode */
		if (inode > 0) {
			if (fstat(fileno(tasks), &a) == -1) {
				log_warn("cgroups", "unable to stat %s",
				    taskspath);
				goto end;
			}
			struct dirent *dirent;
			while ((dirent = readdir(dir))) {
				if (dirent->d_ino == a.st_ino) break;
			}
			if (dirent == NULL) {
				log_debug("cgroups", "tasks file %s has changed",
				    taskspath);
				log_debug("cgroups", "task %s has vanished",
				    task);
				rc = 0;
				goto end;
			}
		}
		closedir(dir); dir = NULL;

		log_debug("cgroups", "kill everybody in task %s", task);
		pid_t pid;
		while (fscanf(tasks, "%d", &pid) == 1) {
			int found = 0;
			struct one_pid *new;
			TAILQ_FOREACH(new, &pids, next) {
				if (new->pid == pid) {
					found = 1;
					break;
				}
			}
			if (found) continue;
			if ((new = malloc(sizeof(struct one_pid))) == NULL) {
				log_warn("cgroups", "memory allocation failure");
				goto end;
			}
			log_debug("cgroups", "kill PID %d for task %s", pid, task);
			kill(pid, signal);
			new->pid = pid;
			TAILQ_INSERT_TAIL(&pids, new, next);
			done = 0;
		}
		fclose(tasks); tasks = NULL;
	} while (!done);
	log_debug("cgroups", "no mode PID to kill in task %s", task);

	rc = 0;
end:
	while (!TAILQ_EMPTY(&pids)) {
		struct one_pid *first = TAILQ_FIRST(&pids);
		TAILQ_REMOVE(&pids, first, next);
		free(first);
	}
	if (dir != NULL) closedir(dir);
	if (tasks != NULL) fclose(tasks);
	free(dirpath);
	free(taskspath);
	return rc;
}


/**
 * Check if the given named hierarchy exists.
 *
 * @param namespace Namespace to check
 * @return 1 if the namespace exists, 0 otherwise
 */
int
cg_exist_named_hierarchy(const char *namespace)
{
	char *path = NULL;
	if (asprintf(&path, "%s/lanco-%s", CGROOT, namespace) == -1) {
		log_warn("cgroups", "unable to allocate memory for named cgroup");
		return 0;
	}
	/* We only do a minimal mount check. If the hierarchy does not exist,
	 * problems will happen later. */
	if (utils_is_mount_point(path, CGROOT)) {
		log_debug("cgroups", "%s exists", path);
		free(path);
		return 1;
	}
	log_debug("cgroups", "%s does not exist", path);
	free(path);
	return 0;
}

/**
 * Delete the release agent (in /var/run)
 *
 * @param namespace Namespace to process
 * @return 0 on success and -1 on error
 */
static int
cg_delete_release_agent(const char *namespace)
{
	char *command = NULL;
	if (asprintf(&command, RUNPREFIX "/lanco-%s/lanco-release-agent@@%s@@release",
		namespace, namespace) == -1) {
		log_warn("cgroups", "unable to allocate memory for release agent");
		return -1;
	}
	unlink(command);
	free(command);
	return 0;
}

/**
 * Delete a named hierarchy. It should be empty for this to work.
 *
 * @param name Name of the hierarchy.
 * @return 0 on success and -1 on error
 */
static int
cg_delete_named_hierarchy(const char *name)
{
	char *path = NULL;
	if (asprintf(&path, "%s/lanco-%s", CGROOT, name) == -1) {
		log_warn("cgroups", "unable to allocate memory for named cgroup");
		free(path);
		return -1;
	}
	if (umount(path) == -1) {
		log_warn("cgroups",
		    "not able to umount cgroup %s", name);
		free(path);
		return -1;
	}
	if (rmdir(path) == -1) {
		log_warn("cgroups",
		    "not able to remove directory %s for cgroup", path);
		free(path);
		return -1;
	}
	free(path);
	return 0;
}

/**
 * Delete cpuacct hierarchy. It should be empty.
 *
 * @param name Name of the hierarchy.
 * @return 0 on success and -1 on error
 */
static int
cg_delete_cpuacct_hierarchy(const char *name)
{
	char *path = NULL;
	if (asprintf(&path, "%s/lanco-%s", CGCPUACCT, name) == -1) {
		log_warn("cgroups", "unable to allocate memory for cpuact cgroup");
		free(path);
		return -1;
	}
	if (rmdir(path) == -1) {
		log_warn("cgroups",
		    "not able to remove directory %s for cgroup", path);
		free(path);
		return -1;
	}
	free(path);
	return 0;
}

/**
 * Delete named and cpuacct hierarchy. They should be empty.
 *
 * @param name Name of the hierarchy.
 * @return 0 on success and -1 on error
 */
int
cg_delete_hierarchies(const char *name)
{
	if (cg_delete_named_hierarchy(name) == -1)
		return -1;
	cg_delete_cpuacct_hierarchy(name);
	cg_delete_release_agent(name);
	return 0;
}

/**
 * Set a property of a given cgroup.
 *
 * @param path     Path to cgroup.
 * @param property Property to change.
 * @param value    Value to set.
 * @return 0 on success and -1 on error
 *
 * This is really `echo value > path/property`.
 */
static int
cg_set_property(const char *path, const char *property, const char*value)
{
	log_debug("cgroups", "setting property %s=%s in %s",
	    property, value, path);
	char *fpath = NULL;
	if (asprintf(&fpath, "%s/%s", path, property) == -1) {
		log_warn("cgroups", "unable to allocate memory to set property");
		return -1;
	}

	FILE *fproperty = fopen(fpath, "w");
	if (fproperty == NULL) {
		log_warn("cgroups", "unable to open %s", fpath);
		free(fpath);
		return -1;
	}
	if (fprintf(fproperty, "%s", value) < 0) {
		log_warn("cgroups", "unable to write to %s", fpath);
		fclose(fproperty);
		free(fpath);
		return -1;
	}
	free(fpath);
	fclose(fproperty);
	return 0;
}

/**
 * Setup release agent for a named hierarchy.
 *
 * Release agent needs to be an executable. It is not possible to provide static
 * arguments. Therefore, we will setup a link to lanco with some special name
 * containing the whole command.
 *
 * @param name Name of the hierarchy
 * @param path Path to the hierarchy
 * @return 0 on success and -1 on error
 */
static int
cg_set_release_agent(const char *name, const char *path)
{
	/* We need to get the executable path. This is not portable, we use
	 * /proc/self/exe. */
	int rc;
	char self[256] = {};
	rc = readlink("/proc/self/exe", self, sizeof(self) - 1);
	if (rc == -1 || rc >= sizeof(self) - 1) {
		log_warn("cgroups", "unable to get self name");
		return -1;
	}

	char *command = NULL;
	if (asprintf(&command, RUNPREFIX "/lanco-%s/lanco-release-agent@@%s@@release",
		name, name) == -1) {
		log_warn("cgroups", "unable to allocate memory for release agent");
		return -1;
	}

	/* We need a symbolic link */
	struct stat a;
	if (lstat(command, &a) != -1) {
		if (!S_ISLNK(a.st_mode)) {
			log_warnx("cgroups",
			    "%s already exists and is not a symlink", command);
			free(command);
			return -1;
		}
		log_debug("cgroups", "symbolic link %s already here", command);
		char link[256] = {};
		rc = readlink(command, link, sizeof(link) - 1);
		if (rc == -1 || rc >= sizeof(link) - 1 ||
		    strcmp(link, self)) {
			log_warnx("cgroups",
			    "symbolic link %s already exists but is incorrect",
			    command);
			free(command);
			return -1;
		}
	} else {
		if (symlink(self, command) == -1) {
			log_warn("cgroups",
			    "unable to setup %s symlink", command);
			free(command);
			return -1;
		}
	}

	/* Register the release agent */
	if (cg_set_property(path, "release_agent", command) == -1) {
		free(command);
		return -1;
	}
	free(command);
	return 0;
}

/**
 * Create and attach an empty named hierarchy.
 *
 * @param name Name of the hierarchy.
 * @param uid UID of the user owning the named hierarchy.
 * @param gid GID of the group owning the named hierarchy.
 *
 * @return 0 on success, -1 on error.
 */
static int
cg_setup_named_hierarchy(const char *name, uid_t uid, gid_t gid)
{
	char *path = NULL;
	char *options = NULL;
	if (asprintf(&path, "%s/lanco-%s", CGROOT, name) == -1 ||
	    asprintf(&options, "none,name=lanco-%s", name) == -1) {
		log_warn("cgroups", "unable to allocate memory for named cgroup");
		free(path);
		return -1;
	}

	log_debug("cgroups", "check if cgroup lanco-%s already exists", name);
	if (utils_is_mount_point(path, CGROOT)) {
		free(options);

		struct stat a;
		if (stat(path, &a) == -1) {
			log_warn("cgroups",
			    "cgroup lanco-%s exists but unable to check it", name);
			free(path);
			return -1;
		}
		if ((a.st_uid != (uid == -1)?geteuid():uid) ||
		    (a.st_gid != (gid == -1)?getegid():gid)) {
			log_warnx("cgroups",
			    "cgroup lanco-%s already exists but wrong permissions",
			    name);
			free(path);
			return -1;
		}
		log_debug("cgroups", "cgroup lanco-%s already setup", name);
		free(path);
		return 0;
	}

	log_debug("cgroups", "mount cgroup lanco-%s", name);
	if (mkdir(path, 0755) == -1) {
		log_warn("cgroups", "unable to create named cgroup lanco-%s",
		    name);
		free(path);
		free(options);
		return -1;
	}
	log_debug("cgroups", "mountpoint: %s", path);
	log_debug("cgroups", "options:    %s", options);
	if (mount("cgroup", path, "cgroup",
		MS_NODEV | MS_NOSUID |
		MS_NOEXEC | MS_RELATIME,
		options) == -1) {
		log_warn("cgroups", "unable to mount named cgroup lanco-%s",
		    name);
		rmdir(path);
		free(path);
		free(options);
		return -1;
	}
	free(options);

	/* Set permissions */
	if (cg_fix_permissions(path, uid, gid) == -1) {
		log_warn("cgroups", "unable to assign new cgroup lanco-%s to "
		    "uid:gid %d:%d", name, uid, gid);
		cg_delete_named_hierarchy(name);
		free(path);
		return -1;
	}

	if (cg_set_property(path, "notify_on_release", "1") == -1 ||
	    cg_set_property(path, "cgroup.clone_children", "1") == -1 ||
	    cg_set_release_agent(name, path) == -1) {
		log_warnx("cgroups", "unable to setup new cgroup lanco-%s", name);
		cg_delete_named_hierarchy(name);
		free(path);
		return -1;
	}

	free(path);
	return 0;
}

/**
 * Create and attach a cpuacct hierarchy.
 *
 * @param name Name of the hierarchy.
 * @param uid UID of the user owning the hierarchy.
 * @param gid GID of the group owning the hierarchy.
 *
 * @return 0 on success, -1 on error.
 */
static int
cg_setup_cpuacct_hierarchy(const char *name, uid_t uid, gid_t gid)
{
	char *path = NULL;
	if (asprintf(&path, "%s/lanco-%s", CGCPUACCT, name) == -1) {
		log_warn("cgroups", "unable to allocate memory for cpuacct cgroup");
		free(path);
		return -1;
	}

	log_debug("cgroups", "check if cpuacct cgroup lanco-%s already exists", name);
	struct stat a;
	if (stat(path, &a) == 0 && S_ISDIR(a.st_mode)) {
		if ((a.st_uid != (uid == -1)?geteuid():uid) ||
		    (a.st_gid != (gid == -1)?getegid():gid)) {
			log_warnx("cgroups",
			    "cpuacct cgroup lanco-%s already exists but wrong permissions",
			    name);
			free(path);
			return -1;
		}
		log_debug("cgroups", "cgroup lanco-%s already setup", name);
		free(path);
		return 0;
	}

	log_debug("cgroups", "create cpuacct cgroup lanco-%s", name);
	if (mkdir(path, 0755) == -1) {
		log_warn("cgroups", "unable to create cpuacct cgroup lanco-%s",
		    name);
		free(path);
		return -1;
	}

	if (cg_fix_permissions(path, uid, gid) == -1) {
		log_warn("cgroups", "unable to assign new cpuacct cgroup lanco-%s to "
		    "uid:gid %d:%d", name, uid, gid);
		if (rmdir(path) == -1)
			log_warn("cgroups",
			    "additionally, not able to remove directory for cgroup");
		free(path);
		return -1;
	}

	free(path);
	return 0;
}

/**
 * Setup cgroups hierarchy.
 *
 * A named hierarchy is created and the cpuacct subsystem is initialized.  Not
 * being able to grab cpuacct subsystem is not considered an error.
 *
 * @param namespace Named hierarchy to create.
 * @param uid UID of the user owning the named hierarchy.
 * @param gid GID of the group owning the named hierarchy.
 *
 * Use -1 for UID or GID to not change the owner.
 *
 * @return 0 on success, -1 otherwise
 */
int
cg_setup_hierarchies(const char *namespace, uid_t uid, gid_t gid)
{
	if (!utils_is_mount_point(CGROOT, CGROOTPARENT)) {
		if (!utils_is_empty_dir(CGROOT)) {
			log_warnx("cgroups",
			    CGROOT " is not a mount point and not an empty "
			    "directory");
			return -1;
		}
		log_info("cgroups", "mount tmpfs on " CGROOT);
		if (mount("tmpfs", CGROOT,
			"tmpfs",
			MS_NODEV | MS_NOSUID |
			MS_NOEXEC | MS_RELATIME,
			"mode=755") == -1) {
			log_warn("cgroups",
			    "unable to setup cgroup mountpoint");
			return -1;
		}
	}

	/* Setup a named hierarchy */
	if (cg_setup_named_hierarchy(namespace, uid, gid) == -1)
		return -1;

	/* Try to mount cpuacct */
	if (!utils_is_mount_point(CGCPUACCT, CGROOT)) {
		if (!utils_is_mount_point(CGCPUCPUACCT, CGROOT)) {
			log_debug("cgroups", "initializing cpu,cpuacct subsystem");
			if (mkdir(CGCPUCPUACCT, 0755) == -1) {
				log_warn("cgroups",
				    "unable to create cpu,cpuacct directory");
				return 0;
			}
			if (mount("cgroup", CGCPUCPUACCT,
				"cgroup",
				MS_NODEV | MS_NOSUID |
				MS_NOEXEC | MS_RELATIME,
				"cpu,cpuacct") == -1) {
				log_warn("cgroups",
				    "unable to mount cpu,cpuacct hierarchy");
				rmdir(CGCPUCPUACCT);
				return 0;
			}
		}
		log_debug("cgroups", "symlink " CGCPUCPUACCT " to " CGCPUACCT);
		if (symlink(CGCPUCPUACCT, CGCPUACCT) == -1) {
			log_warn("cgroups",
			    "unable to create symlink for cpuacct hierarchy");
			return 0;
		}
	} else log_debug("cgroups", CGCPUACCT " hierarchy is already here");

	/* Create a subdirectory in it. */
	if (cg_setup_cpuacct_hierarchy(namespace, uid, gid) == -1)
		return -1;

	return 0;
}
