/*	$NetBSD: eisavar.h,v 1.12 1997/07/17 01:08:46 jtk Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou
 * All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *      for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_EISA_EISAVAR_H_
#define	_DEV_EISA_EISAVAR_H_

/*
 * Definitions for EISA autoconfiguration.
 *
 * This file describes types and functions which are used for EISA
 * configuration.  Some of this information is machine-specific, and is
 * separated into eisa_machdep.h.
 */

#include <machine/bus.h>

#include <dev/core/eisa/eisareg.h>		/* For ID register & string info. */

/* 
 * Structures and definitions needed by the machine-dependent header.
 */
struct eisabus_attach_args;

/*
 * Machine-dependent definitions.
 */
#include <machine/eisa/eisa_machdep.h>

typedef int	eisa_slot_t;		/* really only needs to be 4 bits */

/*
 * EISA bus attach arguments.
 */
struct eisabus_attach_args {
	char				*eba_busname;	/* XXX should be common */
	bus_space_tag_t 	eba_iot;		/* eisa i/o space tag */
	bus_space_tag_t 	eba_memt;		/* eisa mem space tag */
	bus_dma_tag_t 		eba_dmat;		/* DMA tag */
	eisa_chipset_tag_t 	eba_ec;
};

/*
 * EISA device attach arguments.
 */
struct eisa_attach_args {
	bus_space_tag_t 	ea_iot;		/* eisa i/o space tag */
	bus_space_tag_t 	ea_memt;	/* eisa mem space tag */
	bus_dma_tag_t 		ea_dmat;	/* DMA tag */
	eisa_chipset_tag_t 	ea_ec;

	eisa_slot_t			ea_slot;
	u_int8_t			ea_vid[EISA_NVIDREGS];
	u_int8_t			ea_pid[EISA_NPIDREGS];
	char				ea_idstring[EISA_IDSTRINGLEN];
};

#include "locators.h"

/*
 * Locators for EISA devices, as specified to config.
 */
#define	EISACF_SLOT				0
#define EISACF_SLOT_DEFAULT 	-1
#define	EISA_UNKNOWN_SLOT		EISACF_SLOT_DEFAULT	/* wildcarded 'slot' */

#endif /* _DEV_EISA_EISAVAR_H_ */
