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

#ifndef _LANCO_H
#define _LANCO_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include "log.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/types.h>

#define LOGPREFIX "/var/log"
#define RUNPREFIX "/var/run"

/* Commands */
int cmd_init   (const char *, int, char * const *);
int cmd_run    (const char *, int, char * const *);
int cmd_stop   (const char *, int, char * const *);
int cmd_check  (const char *, int, char * const *);
int cmd_release(const char *, int, char * const *);
int cmd_ls     (const char *, int, char * const *);
int cmd_top    (const char *, int, char * const *);
int cmd_dump   (const char *, int, char * const *);

/* cgroups.c */
#define CGROOTPARENT "/sys/fs"
#define CGROOT CGROOTPARENT "/cgroup"
#define CGCPUACCT CGROOT "/cpuacct"
#define CGCPUCPUACCT CGROOT "/cpu,cpuacct"
#define CGMEMORY CGROOT "/memory"
int cg_setup_hierarchies(const char *, uid_t, gid_t);
int cg_delete_hierarchies(const char*);
int cg_exist_named_hierarchy(const char*);
int cg_exist_task(const char*, const char*, ino_t *);
int cg_create_task(const char*, const char*);
int cg_release_task(const char*, const char*);
int cg_kill_task(const char*, const char*, ino_t, int);
int cg_iterate_tasks(const char *,
    int(*visit)(const char *, const char *, void *),
    void *);
int cg_iterate_pids(const char *, const char *,
    int(*visit)(const char *, const char *, pid_t, void*),
    void *);
uint64_t cg_cpu_usage(const char*, const char*);
uint64_t cg_memory_usage(const char*, const char*);
int cg_memory_limit(const char*, const char*, long long unsigned);

/* utils.c */
int utils_is_mount_point(const char *, const char *);
int utils_is_empty_dir(const char *);
int utils_is_dir_owned(const char *, uid_t, gid_t);
int utils_is_valid_name(const char *);
int utils_create_subdirectory(const char*, const char*, uid_t, gid_t);
int utils_redirect_output(const char *);
char * utils_cmdline(pid_t);

#endif
