#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "log.h"
#include "params.h"	// logging, monochrome, quiet_level
#include "pids.h"
#include "shm.h"
#include "trinity.h"
#include "utils.h"

#define BUFSIZE 1024

char ANSI_RED[] = "[1;31m";
char ANSI_GREEN[] = "[1;32m";
char ANSI_YELLOW[] = "[1;33m";
char ANSI_BLUE[] = "[1;34m";
char ANSI_MAGENTA[] = "[1;35m";
char ANSI_CYAN[] = "[1;36m";
char ANSI_WHITE[] = "[1;37m";
char ANSI_RESET[] = "[0m";

static FILE *mainlogfile;
static bool logfiles_opened = FALSE;

void open_logfiles(void)
{
	unsigned int i;
	char *logfilename;

	logfilename = zmalloc(64);
	sprintf(logfilename, "trinity.log");
	unlink(logfilename);
	mainlogfile = fopen(logfilename, "a");
	if (!mainlogfile) {
		outputerr("## couldn't open logfile %s\n", logfilename);
		exit(EXIT_FAILURE);
	}

	for_each_child(i) {
		struct childdata *child = &shm->children[i];

		sprintf(logfilename, "trinity-child%u.log", i);
		unlink(logfilename);
		child->logfile = fopen(logfilename, "a");
		if (!child->logfile) {
			outputerr("## couldn't open logfile %s\n", logfilename);
			exit(EXIT_FAILURE);
		}
	}
	free(logfilename);
	logfiles_opened = TRUE;
}

void close_logfiles(void)
{
	unsigned int i;

	for_each_child(i) {
		struct childdata *child = &shm->children[i];

		if (child->logfile != NULL)
			fclose(child->logfile);
	}
}

static FILE * find_logfile_handle(void)
{
	pid_t pid;
	int i;

	pid = getpid();
	if (pid == initpid)
		return mainlogfile;

	if (pid == shm->mainpid)
		return mainlogfile;

	if (pid == watchdog_pid)
		return mainlogfile;

	i = find_childno(pid);
	if (i != CHILD_NOT_FOUND)
		return shm->children[i].logfile;
	else {
		/* try one more time. FIXME: This is awful. */
		unsigned int j;

		sleep(1);
		i = find_childno(pid);
		if (i != CHILD_NOT_FOUND)
			return shm->children[i].logfile;

		outputerr("## Couldn't find logfile for pid %d\n", pid);
		dump_childnos();
		outputerr("## Logfiles for pids: ");
		for_each_child(j)
			outputerr("%p ", shm->children[j].logfile);
		outputerr("\n");
	}
	return NULL;
}

unsigned int highest_logfile(void)
{
	FILE *file;
	int ret;

	if (logging == FALSE)
		return 0;

	file = shm->children[max_children - 1].logfile;
	ret = fileno(file);

	return ret;
}

void synclogs(void)
{
	unsigned int i;
	int fd;

	if (logging == FALSE)
		return;

	for_each_child(i) {
		struct childdata *child = &shm->children[i];
		int ret;

		if (child->logdirty == FALSE)
			continue;

		child->logdirty = FALSE;

		ret = fflush(child->logfile);
		if (ret == EOF) {
			outputerr("## logfile flushing failed! %s\n", strerror(errno));
			continue;
		}

		fd = fileno(child->logfile);
		if (fd != -1) {
			ret = fsync(fd);
			if (ret != 0)
				outputerr("## fsyncing logfile %d failed. %s\n", i, strerror(errno));
		}
	}

	(void)fflush(mainlogfile);
	fsync(fileno(mainlogfile));
}

FILE *robust_find_logfile_handle(void)
{
	FILE *handle = NULL;

	if ((logging == TRUE) && (logfiles_opened)) {
		handle = find_logfile_handle();
		if (!handle) {
			unsigned int j;

			outputerr("## child logfile handle was null logging to main!\n");
			(void)fflush(stdout);
			for_each_child(j)
				shm->children[j].logfile = mainlogfile;
			sleep(5);
			handle = find_logfile_handle();
		}
	}
	return handle;
}

void strip_ansi(char *ansibuf, unsigned int buflen)
{
	char *from = ansibuf, *to = ansibuf;
	unsigned int len, i;

	/* If we've specified monochrome, we won't have any ANSI codes
	 * in the buffer to be stripped out. */
	if (monochrome == TRUE)
		return;

	/* copy buffer, sans ANSI codes */
	len = strlen(ansibuf);

	for (i = 0; (i < len) && (i + 2 < buflen); i++) {
		*to = from[i];
		if (from[i] == '') {
			if (from[i + 2] == '1')
				i += 6;	// ANSI_COLOUR
			else
				i += 3;	// ANSI_RESET
		} else {
			to++;
		}
	}
	*to = 0;
}

/*
 * level defines whether it gets displayed to the screen with printf.
 * (it always logs).
 *   0 = everything, even all the registers
 *   1 = Watchdog prints syscall count
 *   2 = Just the reseed values
 *
 */
void output(unsigned char level, const char *fmt, ...)
{
	va_list args;
	int n;
	FILE *handle;
	pid_t pid;
	char outputbuf[BUFSIZE];
	char *prefix = NULL;
	char watchdog_prefix[]="[watchdog]";
	char init_prefix[]="[init]";
	char main_prefix[]="[main]";
	char child_prefix[32];

	if (logging == FALSE && level >= quiet_level)
		return;

	/* prefix preparation */
	pid = getpid();
	if (pid == watchdog_pid)
		prefix = watchdog_prefix;

	if (pid == initpid)
		prefix = init_prefix;

	if (pid == shm->mainpid)
		prefix = main_prefix;

	if (prefix == NULL) {
		unsigned int childno;

		childno = find_childno(pid);
		snprintf(child_prefix, sizeof(child_prefix), "[child%u:%u]", childno, pid);
		prefix = child_prefix;
		shm->children[childno].logdirty = TRUE;
	}

	/* formatting output */
	va_start(args, fmt);
	n = vsnprintf(outputbuf, sizeof(outputbuf), fmt, args);
	va_end(args);
	if (n < 0) {
		outputerr("## Something went wrong in output() [%d]\n", n);
		if (getpid() == shm->mainpid)
			exit_main_fail();
		else
			exit(EXIT_FAILURE);
	}

	/* stdout output if needed */
	if (quiet_level > level) {
		printf("%s %s", prefix, outputbuf);
		(void)fflush(stdout);
	}

	/* go on with file logs only if enabled */
	if (logging == FALSE)
		return;

	handle = robust_find_logfile_handle();
	if (!handle)
		return;

	strip_ansi(outputbuf, BUFSIZE);

	fprintf(handle, "%s %s", prefix, outputbuf);

	(void)fflush(handle);
}

/*
* Used as a way to consolidated all printf calls if someones one to redirect it to somewhere else.
* note: this function ignores quiet_level since it main purpose is error output.
*/
void outputerr(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

void outputstd(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
}

/*
 * debugging output.
 * This is just a convenience helper to avoid littering the code
 * with dozens of 'if debug == TRUE' comparisons causing unnecessary nesting.
 */
void debugf(const char *fmt, ...)
{
	char debugbuf[BUFSIZE];
	va_list args;

	if (debug == FALSE)
		return;

	va_start(args, fmt);
	vsprintf(debugbuf, fmt, args);
	va_end(args);
	output(0, debugbuf);
}
