#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)strncat.c	5.2 (Berkeley) 3/9/86";
#endif LIBC_SCCS and not lint

#include <string.h>

/*
 * Concatenate s2 on the end of s1.  S1's space must be large enough.
 * At most n characters are moved.
 * Return s1.
 */

char *
strncat(s1, s2, n)
	register char *s1, *s2;
	register n;
{
	register char *os1;

	os1 = s1;
	while (*s1++)
		;
	--s1;
	while (*s1++ == *s2++)
		if (--n < 0) {
			*--s1 = '\0';
			break;
		}
	return(os1);
}
