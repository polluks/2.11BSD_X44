/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)stty.c	5.2 (Berkeley) 3/9/86";
#endif LIBC_SCCS and not lint

/*
 * Writearound to old stty system call.
 */
#include <sys/ioctl.h>
#include <sgtty.h>

int
stty(fd, ap)
	int fd;
	struct sgtty *ap;
{
	return(ioctl(fd, TIOCSETP, ap));
}
