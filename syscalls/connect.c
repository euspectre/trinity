/*
 * SYSCALL_DEFINE3(connect, int, fd, struct sockaddr __user *, uservaddr, int, addrlen
 *
 * If the connection or binding succeeds, zero is returned.
 * On error, -1 is returned, and errno is set appropriately.
 */
#include "net.h"
#include "sanitise.h"

static void sanitise_connect(struct syscallrecord *rec)
{
	rec->a1 = generic_fd_from_socketinfo((struct socketinfo *) rec->a1);
}

struct syscallentry syscall_connect = {
	.name = "connect",
	.num_args = 3,
	.arg1name = "fd",
	.arg1type = ARG_SOCKETINFO,
	.arg2name = "uservaddr",
	.arg2type = ARG_SOCKADDR,
	.arg3name = "addrlen",
	.arg3type = ARG_SOCKADDRLEN,
	.rettype = RET_ZERO_SUCCESS,
	.flags = NEED_ALARM,
	.sanitise = sanitise_connect,
};
