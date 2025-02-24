/*	$NetBSD: dirname.c,v 1.2 2000/01/22 22:19:09 mycroft Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Klaus Klein.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: dirname.c,v 1.2 2000/01/22 22:19:09 mycroft Exp $");
#endif /* !LIBC_SCCS && !lint */

#include "namespace.h"
#include <sys/param.h>
#include <libgen.h>
#include <limits.h>
#include <string.h>

#ifdef __weak_alias
__weak_alias(dirname,_dirname)
#endif


#if !HAVE_DIRNAME
char *
dirname(path)
	char *path;
{
	static char singledot[] = ".";
	char *p;

	/*
	 * If `path' is a null pointer or points to an empty string, or does
	 * not contain a '/', return a pointer to the string ".".
	 */
	if ((path == NULL) || (*path == '\0') || (strchr(path, '/') == NULL))
		return (singledot);

	/* Strip trailing slashes, if any. */
	p = path + strlen(path) - 1;
	while (*p == '/' && p != path)
		*p-- = '\0';

	/* Terminate path at the last occurence of '/'. */
	if ((p = strrchr(path, '/')) != NULL)
		*(p + (p == path ? 1 : 0)) = '\0';

	return (path);
}
#endif
