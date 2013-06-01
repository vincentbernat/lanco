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

#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <time.h>
#include <curses.h>
#include <syslog.h>
#include <signal.h>
#include <sys/queue.h>

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

struct one_task {
	TAILQ_ENTRY (one_task) next;
	int valid;		/* Is the task still valid? */
	char *name;		/* Task name */
	unsigned nb;		/* Number of processes */
	double cpu_percent;	/* cpu usage in percent */
	uint64_t cpu_usage;	/* absolute CPU usage */
	struct timespec ts;	/* Timestamp of last refresh */
};

static int
one_pid(const char *namespace, const char *name, pid_t pid, void *arg)
{
	struct one_task *task = arg;
	task->nb++;
	return 0;
}

static int
one_task(const char *namespace, const char *name, void *arg)
{
	TAILQ_HEAD(, one_task) *tasks = arg;
	struct one_task *task;
	TAILQ_FOREACH(task, tasks, next)
	    if (!strcmp(task->name, name)) break;
	if (task == NULL) {
		if ((task = malloc(sizeof(struct one_task))) == NULL) return 0;
		task->name = strdup(name);
		TAILQ_INSERT_TAIL(tasks, task, next);
	}
	task->valid = 1;
	task->nb = 0;

	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		log_warn("top", "unable to get current time");
		return -1;
	}

	static int nbcpu = 0;
	if (nbcpu == 0) {
		nbcpu = sysconf(_SC_NPROCESSORS_ONLN);
		if (nbcpu <= 0) nbcpu = 1;
	}

	uint64_t new_usage = cg_cpu_usage(namespace, name);
	if (task->ts.tv_sec && new_usage > 0) {
		uint64_t x, y;

		x = ((uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec) -
		    ((uint64_t) task->ts.tv_sec * 1000000000ULL + (uint64_t) task->ts.tv_nsec);

		y = new_usage - task->cpu_usage;

		if (y > 0) {
			task->cpu_percent = (double) y * (double) 100. / (double) x / (double)nbcpu;
                } else
			task->cpu_percent = 0;
	} else
		task->cpu_percent = 0;
	task->cpu_usage = new_usage;
	memcpy(&task->ts, &ts, sizeof(struct timespec));

	if (cg_iterate_pids(namespace, name, one_pid, task) == -1) {
		return -1;
	}

	return 0;
}

/* Logs handling in curses mode */
static void
curses_log(int severity, const char *msg, void *arg)
{
	const char *prefix = "[UNKN]";
	WINDOW *logs_win = arg;
	int color = 0;
	if (logs_win == NULL) return;
	switch (severity) {
	case LOG_EMERG:   color = 2; prefix = "[EMRG]"; break;
	case LOG_ALERT:   color = 2; prefix = "[ALRT]"; break;
	case LOG_CRIT:    color = 2; prefix = "[CRIT]"; break;
	case LOG_ERR:     color = 2; prefix = "[ ERR]"; break;
	case LOG_WARNING: color = 5; prefix = "[WARN]"; break;
	case LOG_NOTICE:  color = 4; prefix = "[NOTI]"; break;
	case LOG_INFO:    color = 4; prefix = "[INFO]"; break;
	case LOG_DEBUG:   color = 6; prefix = "[ DBG]"; break;
	}

	wattron(logs_win, COLOR_PAIR(color) | A_BOLD);
	wprintw(logs_win, "\n%s", prefix);
	wattroff(logs_win, COLOR_PAIR(color) | A_BOLD);
	wprintw(logs_win, " %s", msg);
	wrefresh(logs_win);
}

static void
curses_init(void)
{
	initscr();
	noecho();
	start_color();
	init_pair(1, COLOR_BLACK, COLOR_GREEN);
	init_pair(2, COLOR_RED, COLOR_BLACK);
	init_pair(3, COLOR_GREEN, COLOR_BLACK);
	init_pair(4, COLOR_BLUE, COLOR_BLACK);
	init_pair(5, COLOR_YELLOW, COLOR_BLACK);
	init_pair(6, COLOR_CYAN, COLOR_BLACK);
}

static void
curses_gauge(WINDOW *win, float percent, int width)
{
	if (width <= 6) return;
	if (width > 10) {
		int size = width - 6 - 2;
		int nb = percent * size / 100;
		if (nb > size) nb = size;
		if (nb < 0) nb = 0;
		waddch(win, '[' | A_BOLD | COLOR_PAIR(0));
		size -= nb;
		while (nb--)
			waddch(win, '|' | COLOR_PAIR((percent > 80)?2:
				(percent > 70)?5:3));
		while (size--)
			waddch(win, ' ');
		waddch(win, ']' | A_BOLD | COLOR_PAIR(0));
	}
	wattron(win, A_BOLD | COLOR_PAIR(6));
	wprintw(win, " %3.1f%%", percent);
	wattroff(win, A_BOLD | COLOR_PAIR(6));
}

static void
curses_global_cpu(WINDOW *win, const char *namespace, int width)
{
	static uint64_t cpu_usage = 0;
	static struct timespec ts = {};

	static int nbcpu = 0;
	if (nbcpu == 0) {
		nbcpu = sysconf(_SC_NPROCESSORS_ONLN);
		if (nbcpu <= 0) nbcpu = 1;
	}

	uint64_t new_usage;
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
		log_warn("top", "unable to get current time");
		return;
	}
	new_usage = cg_cpu_usage(namespace, NULL);

	uint64_t x, y;
	x = ((uint64_t) now.tv_sec * 1000000000ULL + (uint64_t) now.tv_nsec) -
	    ((uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec);
	y = new_usage - cpu_usage;
	if (y > 0) {
		double percent = (double) y * (double) 100. / (double) x / (double) nbcpu;
		wprintw(win, "  ");
		curses_gauge(win, (percent < 100)?percent:100, width - 4);
		wprintw(win, "\n\n");
	}
	cpu_usage = new_usage;
	memcpy(&ts, &now, sizeof(struct timespec));
}

#define GAUGE_SIZE 30
static void
curses_task(WINDOW *win, struct one_task *task, int width)
{
	int x, y;
	wattron(win, A_BOLD);
	wprintw(win, " %-10s ", task->name);
	wattroff(win, A_BOLD);
	wprintw(win, "%5d proc%s ",
	    task->nb, (task->nb > 1)?"s":" ");
	if (task->cpu_usage > 0) {
		getyx(win, y, x);
		if (x > width - GAUGE_SIZE) {
			wprintw(win, "\n");
			getyx(win, y, x);
		}
		wmove(win, y, width - GAUGE_SIZE - 1); /* May fail */
		curses_gauge(win, task->cpu_percent, GAUGE_SIZE);
	}
	wprintw(win, "\n");
}

static void
curses_tasks(const char *namespace, void *arg)
{
	TAILQ_HEAD(, one_task) *tasks = arg;
	struct one_task *task;
	int nb = 0;
	TAILQ_FOREACH(task, tasks, next)
	    nb++;

	static int initialized = 0;
	if (!initialized) {
		curses_init();
		initialized = 1;
	}

	int height, width;
	getmaxyx(stdscr, height, width);

	static WINDOW *logs_win = NULL;
	if (logs_win == NULL && height > 10) {
		logs_win = newwin(8, width, height - 8, 0);
		scrollok(logs_win, TRUE);
		log_register(curses_log, logs_win);
	} else if (logs_win && height > 10) {
		wresize(logs_win, 8, width);
		wmove(logs_win, height - 8, 0);
		wrefresh(logs_win);
	} else if (logs_win) {
		delwin(logs_win);
		logs_win = NULL;
	}

	/* Status window */
	static WINDOW *status_win = NULL;
	if (status_win == NULL)
		status_win = newwin(1, width, 0, 0);
	else
		wresize(status_win, 1, width);
	werase(status_win);
	wmove(status_win, 0, 0);
	wattron(status_win, COLOR_PAIR(1));
	wattron(status_win, A_BOLD);
	wprintw(status_win, "Namespace: ");
	wattroff(status_win, A_BOLD);
	wprintw(status_win, "%-20s", namespace);
	wattron(status_win, A_BOLD);
	wprintw(status_win, "  Tasks: ");
	wattroff(status_win, A_BOLD);
	wprintw(status_win, "%-5d", nb);
	for (int i=0; i < width; i++)
		waddch(status_win, ' ');

	/* Main window */
	static WINDOW *main_win = NULL;
	if (main_win == NULL)
		main_win = newwin((height > 10)?(height - 8):height - 2, width,
		    2, 0);
	else
		wresize(main_win, (height > 10)?(height - 8):height - 2, width);
	werase(main_win);
	wmove(main_win, 1, 0);

	curses_global_cpu(main_win, namespace, width);
	TAILQ_FOREACH(task, tasks, next)
	    curses_task(main_win, task, width);

	if (logs_win) wrefresh(logs_win);
	if (main_win) wrefresh(main_win);
	if (status_win) wrefresh(status_win);
	refresh();
}

static int done = 0;
static void
stop(int signum)
{
	done = 1;
}
int
cmd_top(const char *namespace, int argc, char * const argv[])
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

	TAILQ_HEAD(, one_task) tasks;
	TAILQ_INIT(&tasks);

	signal(SIGINT, stop);

	do {
		/* Mark current tasks as invalid */
		struct one_task *task, *task_next;
		TAILQ_FOREACH(task, &tasks, next) {
			task->valid = 0;
		}

		/* Refresh */
		if (cg_iterate_tasks(namespace, one_task, &tasks) == -1) {
			log_warnx("ls", "error while walking tasks");
			return -1;
		}

		/* Remove invalid tasks */
		for (task = TAILQ_FIRST(&tasks);
		     task != NULL;
		     task = task_next) {
			task_next = TAILQ_NEXT(task, next);
			if (task->valid == 0) {
				TAILQ_REMOVE(&tasks, task, next);
				free(task->name);
				free(task);
			}
		}

		curses_tasks(namespace, &tasks);

		sleep(1);
	} while (!done);

	endwin();
	return 0;
}
