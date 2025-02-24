/*-
 * Copyright (c) 1982, 1986, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ioccom.h	8.2 (Berkeley) 3/28/94
 * $Id$
 */

#ifndef	_SYS_IOCCOM_H_
#define	_SYS_IOCCOM_H_

/*
 * Ioctl's have the command encoded in the lower word, and the size of
 * any in or out parameters in the upper word.  The high 3 bits of the
 * upper word are used to encode the in/out status of the parameter.
 */
/*
 * Ioctl's have the command encoded in the lower word,
 * and the size of any in or out parameters in the upper
 * word.  The high 2 bits of the upper word are used
 * to encode the in/out status of the parameter; for now
 * we restrict parameters to at most 256 bytes (disklabels are 216 bytes).
 */
#ifdef COMPAT_211BSD
//#define	IOCPARM_MASK		0xff		/* parameters must be < 256 bytes */
#endif
#define	IOCPARM_MASK		0x1fff		/* parameter length, at most 13 bits */
#define	IOCPARM_SHIFT		16
#define	IOCGROUP_SHIFT		8
#define	IOCPARM_LEN(x)		(((x) >> IOCPARM_SHIFT) & IOCPARM_MASK)
#define	IOCBASECMD(x)		((x) & ~(IOCPARM_MASK << IOCPARM_SHIFT))
#define	IOCGROUP(x)			(((x) >> IOCGROUP_SHIFT) & 0xff)

#define	IOCPARM_MAX			NBPG		/* max size of ioctl, mult. of NBPG */
#define	IOC_VOID			(unsigned long)0x20000000	/* no parameters */
#define	IOC_OUT				(unsigned long)0x40000000	/* copy out parameters */
#define	IOC_IN				(unsigned long)0x80000000	/* copy in parameters */
#define	IOC_INOUT			(IOC_IN|IOC_OUT)
#define	IOC_DIRMASK			(unsigned long)0xe0000000	/* mask for IN/OUT/VOID */
/* the 0x20000000 is so we can distinguish new ioctl's from old */

#define	_IOC(inout, group, num, len) \
    ((inout) | (((len) & IOCPARM_MASK) << IOCPARM_SHIFT) | ((group) << IOCGROUP_SHIFT) | (num))
    
#define	_IO(x,y)			_IOC(IOC_VOID, 	(x), (y), 0)
#define	_IOR(x,y,t)			_IOC(IOC_OUT, 	(x), (y), sizeof(t))
#define	_IOW(x,y,t)			_IOC(IOC_IN,	(x), (y), sizeof(t))
/* this should be _IORW, but stdio got there first */
#define	_IOWR(x,y,t)		_IOC(IOC_INOUT,	(x), (y), sizeof(t))
#define	_IOWINT(x,y)	    _IOC(IOC_VOID,	(x), (y), sizeof(int))

#endif /* !_SYS_IOCCOM_H_ */
