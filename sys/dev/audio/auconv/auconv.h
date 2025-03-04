/*	$NetBSD: auconv.h,v 1.3.10.1 1999/04/16 20:26:52 augustss Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson.
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

#ifndef _SYS_DEV_AUCONV_H_
#define _SYS_DEV_AUCONV_H_

/* Convert between signed and unsigned. */
extern void change_sign8(void *, u_char *, int);
extern void change_sign16_le(void *, u_char *, int);
extern void change_sign16_be(void *, u_char *, int);
/* Convert between little and big endian. */
extern void swap_bytes(void *, u_char *, int);
extern void swap_bytes_change_sign16_le(void *, u_char *, int);
extern void swap_bytes_change_sign16_be(void *, u_char *, int);
extern void change_sign16_swap_bytes_le(void *, u_char *, int);
extern void change_sign16_swap_bytes_be(void *, u_char *, int);
/* Byte expansion/contraction */
extern void linear8_to_linear16_le(void *, u_char *, int);
extern void linear8_to_linear16_be(void *, u_char *, int);
extern void linear16_to_linear8_le(void *, u_char *, int);
extern void linear16_to_linear8_be(void *, u_char *, int);
/* Byte expansion/contraction with sign change */
extern void ulinear8_to_slinear16_le(void *, u_char *, int);
extern void ulinear8_to_slinear16_be(void *, u_char *, int);
extern void slinear16_to_ulinear8_le(void *, u_char *, int);
extern void slinear16_to_ulinear8_be(void *, u_char *, int);

#endif /* !_SYS_DEV_AUCONV_H_ */
