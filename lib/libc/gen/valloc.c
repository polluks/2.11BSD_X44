/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)valloc.c	5.2 (Berkeley) 3/9/86";
#endif LIBC_SCCS and not lint

#include <stdlib.h>
#include <unistd.h>

void *
valloc(i)
	size_t i;
{
	int valsiz = getpagesize(), j;
	void *cp = malloc(i + (valsiz-1));

	j = ((int)cp + (valsiz-1)) &~ (valsiz-1);
	return ((void *)j);
}
