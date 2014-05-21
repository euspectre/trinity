#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "epoll.h"
#include "eventfd.h"
#include "files.h"
#include "log.h"
#include "net.h"
#include "params.h"
#include "perf.h"
#include "pids.h"
#include "pipes.h"
#include "random.h"
#include "sanitise.h"
#include "shm.h"
#include "trinity.h"
#include "utils.h"

struct fd_provider {
	int (*open)(void);
	int (*get)(void);
};

static struct fd_provider fd_providers[] = {
	{ .open = &open_sockets, .get = &get_rand_socket_fd },
	{ .open = &open_pipes, .get = &get_rand_pipe_fd },
	{ .open = &open_perf_fds, .get = &get_rand_perf_fd },
	{ .open = &open_epoll_fds, .get = &get_rand_epoll_fd },
	{ .open = &open_eventfd_fds, .get = &get_rand_eventfd_fd },
	{ .open = &open_files, .get = &get_rand_file_fd },
};

static int get_new_random_fd(void)
{
	int fd = -1;

	while (fd < 0) {
		unsigned int i;
		i = rand() % ARRAY_SIZE(fd_providers);
		fd = fd_providers[i].get();
	}

	return fd;
}

int get_random_fd(void)
{
	/* 25% chance of returning something new. */
	if ((rand() % 4) == 0)
		return get_new_random_fd();

	/* the rest of the time, return the same fd as last time. */
regen:
	if (shm->fd_lifetime == 0) {
		shm->current_fd = get_new_random_fd();
		shm->fd_lifetime = rand_range(5, max_children);
	} else
		shm->fd_lifetime--;

	if (shm->current_fd == 0) {
		shm->fd_lifetime = 0;
		goto regen;
	}

	return shm->current_fd;
}

unsigned int setup_fds(void)
{
	int ret = TRUE;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fd_providers); i++) {
		ret = fd_providers[i].open();
		if (ret == FALSE) {
			exit_main_fail();
		}
	}

	return ret;
}
