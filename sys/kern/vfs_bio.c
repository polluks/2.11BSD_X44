/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	from: @(#)vfs_bio.c	8.6 (Berkeley) 1/11/94
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/devsw.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/trace.h>
#include <sys/malloc.h>
#include <sys/resourcevar.h>
#include <sys/conf.h>

#include <vm/include/vm.h>

#include <machine/cpu.h>

/*
 * Definitions for the buffer hash lists.
 */
#define	BUFHASH(dvp, lbn)	\
	(&bufhashtbl[((int)(dvp) / sizeof(*(dvp)) + (int)(lbn)) & (BUFHSZ * ((int)(lbn) + (bufhash)) - 1)])

u_long	bufhash;
int 	needbuffer;

struct bufhd 	*bufhashtbl, invalhash;
struct bqueues 	bufqueues[BQUEUES];

void
bremfree(bp)
	struct buf *bp;
{
	struct bqueues *dp = NULL;

	/*
	 * We only calculate the head of the freelist when removing
	 * the last element of the list as that is the only time that
	 * it is needed (e.g. to reset the tail pointer).
	 *
	 * NB: This makes an assumption about how tailq's are implemented.
	 */
	TAILQ_NEXT(bp, b_freelist);
	if (TAILQ_NEXT(bp, b_freelist) == NULL) {
		for (dp = bufqueues; dp < &bufqueues[BQUEUES]; dp++) {
			if (TAILQ_LAST(dp, bqueues) == TAILQ_NEXT(bp, b_freelist)) {
				break;
			}
		}
		if (dp == &bufqueues[BQUEUES]) {
			panic("bremfree: lost tail");
		}
	}
	TAILQ_REMOVE(dp, bp, b_freelist);
}

/*
 * Initialize buffers and hash links for buffers.
 */
void
bufinit()
{
	register struct buf *bp;
	struct bqueues *dp;
	register int i;
	int base, residual;

	for (dp = bufqueues; dp < &bufqueues[BQUEUES]; dp++) {
		TAILQ_INIT(dp);
	}
	bufhashtbl = hashinit(nbuf, M_CACHE, &bufhash);
	base = bufpages / nbuf;
	residual = bufpages % nbuf;
	for (i = 0; i < nbuf; i++) {
		bp = &buf[i];
		bzero((char *)bp, sizeof *bp);
		bp->b_dev = NODEV;
		bp->b_rcred = NOCRED;
		bp->b_wcred = NOCRED;
		LIST_NEXT(bp, b_vnbufs) = NOLIST;
		bp->b_data = buffers + i * MAXBSIZE;
		if (i < residual) {
			bp->b_bufsize = (base + 1) * CLBYTES;
		} else {
			bp->b_bufsize = base * CLBYTES;
		}
		bp->b_flags = B_INVAL;
		dp = bp->b_bufsize ? &bufqueues[BQ_AGE] : &bufqueues[BQ_EMPTY];
		binsheadfree(bp, dp);
		binshash(bp, &invalhash);
	}
}

struct buf *
bio_doread(vp, blkno, size, cred, async)
	struct vnode *vp;
	daddr_t blkno;
	int size;
	struct ucred *cred;
	int async;
{
	register struct buf *bp;
	struct proc *p = (curproc != NULL ? curproc : &proc0);	/* XXX */

	bp = getblk(vp, blkno, size, 0, 0);

	/*
	 * If buffer does not have data valid, start a read.
	 * Note that if buffer is B_INVAL, getblk() won't return it.
	 * Therefore, it's valid if it's I/O has completed or been delayed.
	 */
	if (!ISSET(bp->b_flags, (B_DONE | B_DELWRI))) {
		/* Start I/O for the buffer (keeping credentials). */
		SET(bp->b_flags, B_READ | async);
		if (cred != NOCRED && bp->b_rcred == NOCRED) {
			crhold(cred);
			bp->b_rcred = cred;
		}
		VOP_STRATEGY(bp);

		/* Pay for the read. */
		p->p_stats->p_ru.ru_inblock++;
	} else if (async) {
		brelse(bp);
	}

	return (bp);
}

/*
 * Read a disk block.
 * This algorithm described in Bach (p.54).
 */
int
bread(vp, blkno, size, cred, bpp)
	struct vnode *vp;
	daddr_t blkno;
	int size;
	struct ucred *cred;
	struct buf **bpp;
{
	register struct buf *bp;

	/* Get buffer for block. */
	bp = *bpp = bio_doread(vp, blkno, size, cred, 0);

	/*
	 * Delayed write buffers are found in the cache and have
	 * valid contents. Also, B_ERROR is not set, otherwise
	 * getblk() would not have returned them.
	 */
	if (ISSET(bp->b_flags, B_DONE|B_DELWRI))
		return (0);

	/*
	 * Otherwise, we had to start a read for it; wait until
	 * it's valid and return the result.
	 */
	return (biowait(bp));
}

/*
 * Read-ahead multiple disk blocks. The first is sync, the rest async.
 * Trivial modification to the breada algorithm presented in Bach (p.55).
 */
int
breadn(vp, blkno, size, rablks, rasizes, nrablks, cred, bpp)
	struct vnode *vp;
	daddr_t blkno; int size;
	daddr_t rablks[]; int rasizes[];
	int nrablks;
	struct ucred *cred;
	struct buf **bpp;
{
	register struct buf *bp;
	int i;

	bp = *bpp = bio_doread(vp, blkno, size, cred, 0);

	/*
	 * For each of the read-ahead blocks, start a read, if necessary.
	 */
	for (i = 0; i < nrablks; i++) {
		/* If it's in the cache, just go on to next one. */
		if (incore(vp, rablks[i]))
			continue;

		/* Get a buffer for the read-ahead block */
		(void) bio_doread(vp, rablks[i], rasizes[i], cred, B_ASYNC);
	}

	/*
	 * Delayed write buffers are found in the cache and have
	 * valid contents. Also, B_ERROR is not set, otherwise
	 * getblk() would not have returned them.
	 */
	if (ISSET(bp->b_flags, B_DONE | B_DELWRI))
		return (0);

	/*
	 * Otherwise, we had to start a read for it; wait until
	 * it's valid and return the result.
	 */
	return (biowait(bp));
}

/*
 * Modified 2.11BSD breada
 * Read in the block, like bread, but also start I/O on the
 * read-ahead block (which is not allocated to the caller)
 */
int
breada(vp, blkno, rablkno, size, cred, bpp)
	struct vnode *vp;
	daddr_t blkno;
	daddr_t rablkno;
	int size;
	struct ucred *cred;
	struct buf **bpp;
{
	struct buf *bp, *rabp;
	const struct bdevsw *bdev;

	/*
	 * If the block isn't in core, then allocate
	 * a buffer and initiate i/o (getblk checks
	 * for a cache hit).
	 */
	if (!incore(vp, blkno)) {
		bp = *bpp = bio_doread(vp, blkno, size, cred, 0);
		bdev = bdevsw_lookup(bp->b_dev);
		if (ISSET(bp->b_flags, B_DONE | B_DELWRI)) {
			bp->b_flags |= B_READ;
			bp->b_bcount = DEV_BSIZE; /* XXX? KB */
			(*bdev->d_strategy)(bp);
			trace1(TR_BREADMISS);
			u.u_ru.ru_inblock++; /* pay for read */
		} else {
			trace1(TR_BREADHIT);
		}
	}

	/*
	 * If there's a read-ahead block, start i/o
	 * on it also (as above).
	 */
	if (rablkno) {
		if (!incore(vp, rablkno)) {
			rabp = *bpp = bio_doread(vp, rablkno, size, cred, 0);
			bdev = bdevsw_lookup(rabp->b_dev);
			if (ISSET(rabp->b_flags, B_DONE | B_DELWRI)) {
				brelse(rabp);
				trace1(TR_BREADHITRA);
			} else {
				rabp->b_flags |= B_READ | B_ASYNC;
				rabp->b_bcount = DEV_BSIZE; /* XXX? KB */
				(*bdev->d_strategy)(rabp);
				trace1(TR_BREADMISSRA);
				u.u_ru.ru_inblock++; /* pay in advance */
			}
		} else {
			trace1(TR_BREADHITRA);
		}
	}

	/*
	 * If block was in core, let bread get it.
	 * If block wasn't in core, then the read was started
	 * above, and just wait for it.
	 */
	if (bp == NULL) {
		return (bread(vp, blkno, size, cred, &bp));
	}
	return (biowait(bp));
}

/*
 * Block write.  Described in Bach (p.56)
 */
int
bwrite(bp)
	struct buf *bp;
{
	int rv, sync, wasdelayed, s;
	struct proc *p = (curproc != NULL ? curproc : &proc0);	/* XXX */

	/*
	 * Remember buffer type, to switch on it later.  If the write was
	 * synchronous, but the file system was mounted with MNT_ASYNC,
	 * convert it to a delayed write.
	 * XXX note that this relies on delayed tape writes being converted
	 * to async, not sync writes (which is safe, but ugly).
	 */
	sync = !ISSET(bp->b_flags, B_ASYNC);
	if (sync && bp->b_vp && bp->b_vp->v_mount &&
	    ISSET(bp->b_vp->v_mount->mnt_flag, MNT_ASYNC)) {
		bdwrite(bp);
		return (0);
	}

	wasdelayed = ISSET(bp->b_flags, B_DELWRI);
	CLR(bp->b_flags, (B_READ | B_DONE | B_ERROR | B_DELWRI));

	s = splbio();

	/*
	 * Pay for the I/O operation and make sure the buf is on the correct
	 * vnode queue.
	 */
	if (wasdelayed)
		reassignbuf(bp, bp->b_vp);
	else
		p->p_stats->p_ru.ru_oublock++;

	/* Initiate disk write.  Make sure the appropriate party is charged. */
	bp->b_vp->v_numoutput++;
	splx(s);

	SET(bp->b_flags, B_WRITEINPROG);
	VOP_STRATEGY(bp);

	if (sync) {
		/* If I/O was synchronous, wait for it to complete. */
		rv = biowait(bp);

		/* Release the buffer. */
		brelse(bp);

		return (rv);
	} else {
		return (0);
	}
}

int
vn_bwrite(ap)
	struct vop_bwrite_args *ap;
{
	return (bwrite(ap->a_bp));
}

/*
 * Delayed write.
 *
 * The buffer is marked dirty, but is not queued for I/O.
 * This routine should be used when the buffer is expected
 * to be modified again soon, typically a small write that
 * partially fills a buffer.
 *
 * NB: magnetic tapes cannot be delayed; they must be
 * written in the order that the writes are requested.
 *
 * Described in Leffler, et al. (pp. 208-213).
 */
void
bdwrite(bp)
	struct buf *bp;
{
	int s;
	struct proc *p = (curproc != NULL ? curproc : &proc0);	/* XXX */
	const struct bdevsw *bdev;
	bdev = bdevsw_lookup(bp->b_dev);

	/* If this is a tape block, write the block now. */
	/* XXX NOTE: the memory filesystem usurpes major device */
	/* XXX       number 255, which is a bad idea.		*/
	if (bp->b_dev != NODEV &&
	    major(bp->b_dev) != 255 &&	/* XXX - MFS buffers! */
		bdev->d_type == D_TAPE) {
		bawrite(bp);
		return;
	}

	/*
	 * If the block hasn't been seen before:
	 *	(1) Mark it as having been seen,
	 *	(2) Charge for the write,
	 *	(3) Make sure it's on its vnode's correct block list.
	 */
	if (!ISSET(bp->b_flags, B_DELWRI)) {
		SET(bp->b_flags, B_DELWRI);
		p->p_stats->p_ru.ru_oublock++;
		s = splbio();
		reassignbuf(bp, bp->b_vp);
		splx(s);
	}

	/* Otherwise, the "write" is done, so mark and release the buffer. */
	CLR(bp->b_flags, B_NEEDCOMMIT|B_DONE);
	brelse(bp);
}

/*
 * Asynchronous block write; just an asynchronous bwrite().
 */
void
bawrite(bp)
	struct buf *bp;
{

	SET(bp->b_flags, B_ASYNC);
	VOP_BWRITE(bp);
}

/*
 * Release a buffer on to the free lists.
 * Described in Bach (p. 46).
 */
void
brelse(bp)
	struct buf *bp;
{
	struct bqueues *bufq;
	int s;

	/* Wake up any processes waiting for any buffer to become free. */
	if (needbuffer) {
		needbuffer = 0;
		wakeup(&needbuffer);
	}

	/* Wake up any proceeses waiting for _this_ buffer to become free. */
	if (ISSET(bp->b_flags, B_WANTED)) {
		CLR(bp->b_flags, B_WANTED|B_AGE);
		wakeup(bp);
	}

	/* Block disk interrupts. */
	s = splbio();

	/*
	 * Determine which queue the buffer should be on, then put it there.
	 */

	/* If it's locked, don't report an error; try again later. */
	if (ISSET(bp->b_flags, (B_LOCKED|B_ERROR)) == (B_LOCKED|B_ERROR))
		CLR(bp->b_flags, B_ERROR);

	/* If it's not cacheable, or an error, mark it invalid. */
	if (ISSET(bp->b_flags, (B_NOCACHE|B_ERROR)))
		SET(bp->b_flags, B_INVAL);

	if (ISSET(bp->b_flags, B_VFLUSH)) {
		/*
		 * This is a delayed write buffer that was just flushed to
		 * disk.  It is still on the LRU queue.  If it's become
		 * invalid, then we need to move it to a different queue;
		 * otherwise leave it in its current position.
		 */
		CLR(bp->b_flags, B_VFLUSH);
		if (!ISSET(bp->b_flags, B_ERROR|B_INVAL|B_LOCKED|B_AGE)) {
			goto already_queued;
		} else {
			bremfree(bp);
		}
	}

	if ((bp->b_bufsize <= 0) || ISSET(bp->b_flags, B_INVAL)) {
		/*
		 * If it's invalid or empty, dissociate it from its vnode
		 * and put on the head of the appropriate queue.
		 */
		if (bp->b_vp) {
			brelvp(bp);
		}
		CLR(bp->b_flags, B_DONE|B_DELWRI);
		if (bp->b_bufsize <= 0) {
			/* no data */
			bufq = &bufqueues[BQ_EMPTY];
		} else {
			/* invalid data */
			bufq = &bufqueues[BQ_AGE];
		}
		binsheadfree(bp, bufq);
	} else {
		/*
		 * It has valid data.  Put it on the end of the appropriate
		 * queue, so that it'll stick around for as long as possible.
		 */
		if (ISSET(bp->b_flags, B_LOCKED))
			/* locked in core */
			bufq = &bufqueues[BQ_LOCKED];
		else if (ISSET(bp->b_flags, B_AGE))
			/* stale but valid data */
			bufq = &bufqueues[BQ_AGE];
		else
			/* valid data */
			bufq = &bufqueues[BQ_LRU];
		binstailfree(bp, bufq);
	}

already_queued:
	/* Unlock the buffer. */
	CLR(bp->b_flags, B_AGE|B_ASYNC|B_BUSY|B_NOCACHE);

	/* Allow disk interrupts. */
	splx(s);
}

/*
 * Determine if a block is in the cache.
 * Just look on what would be its hash chain.  If it's there, return
 * a pointer to it, unless it's marked invalid.  If it's marked invalid,
 * we normally don't return the buffer, unless the caller explicitly
 * wants us to.
 */
struct buf *
incore(vp, blkno)
	struct vnode *vp;
	daddr_t blkno;
{
	struct buf *bp;

	bp = LIST_FIRST(BUFHASH(vp, blkno));

	/* Search hash chain */

	for (; bp != NULL; bp = LIST_NEXT(bp, b_hash)) {
		if (bp->b_lblkno == blkno && bp->b_vp == vp && !ISSET(bp->b_flags, B_INVAL)) {
			return (bp);
		}
	}

	return (0);
}

/*
 * Get a block of requested size that is associated with
 * a given vnode and block offset. If it is found in the
 * block cache, mark it as having been found, make it busy
 * and return it. Otherwise, return an empty block of the
 * correct size. It is up to the caller to insure that the
 * cached blocks be of the correct size.
 */
struct buf *
getblk(vp, blkno, size, slpflag, slptimeo)
	register struct vnode *vp;
	daddr_t blkno;
	int size, slpflag, slptimeo;
{
	struct bufhd *bh;
	struct buf *bp;
	int s, err;

	/*
	 * XXX
	 * The following is an inlined version of 'incore()', but with
	 * the 'invalid' test moved to after the 'busy' test.  It's
	 * necessary because there are some cases in which the NFS
	 * code sets B_INVAL prior to writing data to the server, but
	 * in which the buffers actually contain valid data.  In this
	 * case, we can't allow the system to allocate a new buffer for
	 * the block until the write is finished.
	 */
	bh = BUFHASH(vp, blkno);
start:
    bp = LIST_FIRST(bh);
	for (; bp != NULL; bp = LIST_NEXT(bp, b_hash)) {
		if (bp->b_lblkno != blkno || bp->b_vp != vp)
			continue;

		s = splbio();
		if (ISSET(bp->b_flags, B_BUSY)) {
			SET(bp->b_flags, B_WANTED);
			err = tsleep(bp, slpflag | (PRIBIO + 1), "getblk", slptimeo);
			splx(s);
			if (err)
				return (NULL);
			goto start;
		}

		if (!ISSET(bp->b_flags, B_INVAL)) {
#ifdef DIAGNOSTIC
			if (ISSET(bp->b_flags, B_DONE|B_DELWRI) &&
			    bp->b_bcount < size)
				panic("getblk: block size invariant failed");
#endif
			SET(bp->b_flags, B_BUSY);
			bremfree(bp);
			splx(s);
			break;
		}
		splx(s);
	}

	if (bp == NULL) {
		if ((bp = getnewbuf(slpflag, slptimeo)) == NULL)
			goto start;
		binshash(bp, bh);
		bp->b_blkno = bp->b_lblkno = blkno;
		s = splbio();
		bgetvp(vp, bp);
		splx(s);
	}
	allocbuf(bp, size);
	return (bp);
}

/*
 * Get an empty, disassociated buffer of given size.
 */
struct buf *
geteblk(size)
	int size;
{
	struct buf *bp;

	while ((bp = getnewbuf(0, 0)) == 0)
		;
	SET(bp->b_flags, B_INVAL);
	binshash(bp, &invalhash);
	allocbuf(bp, size);

	return (bp);
}

/*
 * Expand or contract the actual memory allocated to a buffer.
 *
 * If the buffer shrinks, data is lost, so it's up to the
 * caller to have written it out *first*; this routine will not
 * start a write.  If the buffer grows, it's the callers
 * responsibility to fill out the buffer's additional contents.
 */
void
allocbuf(bp, size)
	struct buf *bp;
	int size;
{
	struct buf      *nbp;
	size_t       	desired_size;
	int	     		s;

	desired_size = roundup(size, CLBYTES);
	if (desired_size > MAXBSIZE)
		panic("allocbuf: buffer larger than MAXBSIZE requested");

	if (bp->b_bufsize == desired_size)
		goto out;

	/*
	 * If the buffer is smaller than the desired size, we need to snarf
	 * it from other buffers.  Get buffers (via getnewbuf()), and
	 * steal their pages.
	 */
	while (bp->b_bufsize < desired_size) {
		int amt;

		/* find a buffer */
		while ((nbp = getnewbuf(0, 0)) == NULL)
			;
		SET(nbp->b_flags, B_INVAL);
		binshash(nbp, &invalhash);

		/* and steal its pages, up to the amount we need */
		amt = min(nbp->b_bufsize, (desired_size - bp->b_bufsize));
		pagemove((nbp->b_data + nbp->b_bufsize - amt),
			 bp->b_data + bp->b_bufsize, amt);
		bp->b_bufsize += amt;
		nbp->b_bufsize -= amt;

		/* reduce transfer count if we stole some data */
		if (nbp->b_bcount > nbp->b_bufsize)
			nbp->b_bcount = nbp->b_bufsize;

#ifdef DIAGNOSTIC
		if (nbp->b_bufsize < 0)
			panic("allocbuf: negative bufsize");
#endif

		brelse(nbp);
	}

	/*
	 * If we want a buffer smaller than the current size,
	 * shrink this buffer.  Grab a buf head from the EMPTY queue,
	 * move a page onto it, and put it on front of the AGE queue.
	 * If there are no free buffer headers, leave the buffer alone.
	 */
	if (bp->b_bufsize > desired_size) {
		s = splbio();
		if ((nbp = TAILQ_FIRST(&bufqueues[BQ_EMPTY])) == NULL) {
			/* No free buffer head */
			splx(s);
			goto out;
		}
		bremfree(nbp);
		SET(nbp->b_flags, B_BUSY);
		splx(s);

		/* move the page to it and note this change */
		pagemove(bp->b_data + desired_size, nbp->b_data, bp->b_bufsize - desired_size);
		nbp->b_bufsize = bp->b_bufsize - desired_size;
		bp->b_bufsize = desired_size;
		nbp->b_bcount = 0;
		SET(nbp->b_flags, B_INVAL);

		/* release the newly-filled buffer and leave */
		brelse(nbp);
	}

out:
	bp->b_bcount = size;
}

/*
 * Find a buffer which is available for use.
 * Select something from a free list.
 * Preference is to AGE list, then LRU list.
 */
struct buf *
getnewbuf(slpflag, slptimeo)
	int slpflag, slptimeo;
{
	register struct buf *bp;
	int s;

start:
	s = splbio();
	if ((bp = TAILQ_FIRST(&bufqueues[BQ_AGE])) != NULL ||
	    (bp = TAILQ_FIRST(&bufqueues[BQ_LRU])) != NULL) {
		bremfree(bp);
	} else {
		/* wait for a free buffer of any kind */
		needbuffer = 1;
		tsleep(&needbuffer, slpflag|(PRIBIO+1), "getnewbuf", slptimeo);
		splx(s);
		return (0);
	}

	if (ISSET(bp->b_flags, B_VFLUSH)) {
		/*
		 * This is a delayed write buffer being flushed to disk.  Make
		 * sure it gets aged out of the queue when it's finished, and
		 * leave it off the LRU queue.
		 */
		CLR(bp->b_flags, B_VFLUSH);
		SET(bp->b_flags, B_AGE);
		splx(s);
		goto start;
	}

	/* Buffer is no longer on free lists. */
	SET(bp->b_flags, B_BUSY);

	/* If buffer was a delayed write, start it, and go back to the top. */
	if (ISSET(bp->b_flags, B_DELWRI)) {
		splx(s);
		/*
		 * This buffer has gone through the LRU, so make sure it gets
		 * reused ASAP.
		 */
		SET(bp->b_flags, B_AGE);
		bawrite(bp);
		goto start;
	}

	/* disassociate us from our vnode, if we had one... */
	if (bp->b_vp)
		brelvp(bp);
	splx(s);

	/* clear out various other fields */
	bp->b_flags = B_BUSY;
	bp->b_dev = NODEV;
	bp->b_blkno = bp->b_lblkno = 0;
	bp->b_iodone = 0;
	bp->b_error = 0;
	bp->b_resid = 0;
	bp->b_bcount = 0;
	bp->b_dirtyoff = bp->b_dirtyend = 0;
	bp->b_validoff = bp->b_validend = 0;

	/* nuke any credentials we were holding */
	if (bp->b_rcred != NOCRED) {
		crfree(bp->b_rcred);
		bp->b_rcred = NOCRED;
	}
	if (bp->b_wcred != NOCRED) {
		crfree(bp->b_wcred);
		bp->b_wcred = NOCRED;
	}

	bremhash(bp);
	return (bp);
}

/*
 * Wait for operations on the buffer to complete.
 * When they do, extract and return the I/O's error value.
 */
int
biowait(bp)
	struct buf *bp;
{
	int s;

	s = splbio();
	while (!ISSET(bp->b_flags, B_DONE))
		tsleep(bp, PRIBIO + 1, "biowait", 0);
	splx(s);

	/* check for interruption of I/O (e.g. via NFS), then errors. */
	if (ISSET(bp->b_flags, B_EINTR)) {
		CLR(bp->b_flags, B_EINTR);
		return (EINTR);
	} else if (ISSET(bp->b_flags, B_ERROR))
		return (bp->b_error ? bp->b_error : EIO);
	else
		return (0);
}

/*
 * Mark I/O complete on a buffer.
 *
 * If a callback has been requested, e.g. the pageout
 * daemon, do so. Otherwise, awaken waiting processes.
 *
 * [ Leffler, et al., says on p.247:
 *	"This routine wakes up the blocked process, frees the buffer
 *	for an asynchronous write, or, for a request by the pagedaemon
 *	process, invokes a procedure specified in the buffer structure" ]
 *
 * In real life, the pagedaemon (or other system processes) wants
 * to do async stuff to, and doesn't want the buffer brelse()'d.
 * (for swap pager, that puts swap buffers on the free lists (!!!),
 * for the vn device, that puts malloc'd buffers on the free lists!)
 */
void
biodone(bp)
	struct buf *bp;
{
	if (ISSET(bp->b_flags, B_DONE)) {
		panic("biodone already");
	}
	SET(bp->b_flags, B_DONE);					/* note that it's done */

	if (!ISSET(bp->b_flags, B_READ)) {			/* wake up reader */
		vwakeup(bp);
	}

	if (ISSET(bp->b_flags, B_CALL)) {			/* if necessary, call out */
		CLR(bp->b_flags, B_CALL);				/* but note callout done */
		(*bp->b_iodone)(bp);
	} else if (ISSET(bp->b_flags, B_ASYNC)) {	/* if async, release it */
		brelse(bp);
	} else {									/* or just wakeup the buffer */
		CLR(bp->b_flags, B_WANTED);
		wakeup(bp);
	}
}

/*
 * Modified 2.11BSD blkflush
 * Insure that no part of a specified block is in an incore buffer.
 */
void
blkflush(vp, blkno, dev)
	struct vnode 	*vp;
	daddr_t 		blkno;
	dev_t 			dev;
{
	struct bufhd		*bhash;
	struct buf 			*ep;
	struct buf 			*dp;
	int s;

	bhash = BUFHASH(vp, blkno);
	dp = LIST_FIRST(bhash);
loop:
	for (ep = dp; ep != dp; ep = LIST_NEXT(ep, b_hash)) {
		if (ep->b_blkno != blkno || ep->b_vp != vp || ep->b_dev != dev || (ep->b_flags & B_INVAL)) {
			continue;
		}
		s = splbio();
		if (ep->b_flags & B_BUSY) {
			ep->b_flags |= B_WANTED;
			sleep((caddr_t)ep, PRIBIO + 1);
			splx(s);
			goto loop;
		}
		if (ep->b_flags & B_DELWRI) {
			splx(s);
			notavail(ep, bhash, b_hash); 	/* XXX: may not work properly!!! */
			bwrite(ep);
			goto loop;
		}
		splx(s);
	}
}

/*
 * Modified 2.11BSD bflush
 * Make sure all write-behind blocks on dev are flushed out.
 * (from umount and sync)
 */
void
bflush(vp, blkno, dev)
	struct vnode 	*vp;
	daddr_t 		blkno;
	dev_t 			dev;
{
	struct bufhd		*bhash;
	struct buf 			*bp;
	struct buf 			*flist;
	int 				s;

loop:
	s = splbio();
	for (flist = TAILQ_FIRST(&bufqueues[BQUEUES]); flist < TAILQ_LAST(&bufqueues[BQ_EMPTY], bqueues); flist = TAILQ_NEXT(flist, b_freelist)) {
		if (flist->b_vp == vp && flist->b_blkno == blkno) {
			bhash = BUFHASH(flist->b_vp, flist->b_blkno);
			for (bp = LIST_FIRST(bhash); bp != flist; bp = LIST_NEXT(bp, b_hash)) {
				if ((bp->b_flags & B_DELWRI) == 0)
					continue;
				if (dev == bp->b_dev) {
					bp->b_flags |= B_ASYNC;
					notavail(bp, bhash, b_hash);
					bwrite(bp);
					splx(s);
					goto loop;
				}
			}
		}
	}
	splx(s);
}

/*
 * 2.11BSD geterror
 * Pick up the device's error number and pass it to the user;
 * if there is an error but the number is 0 set a generalized code.
 */
int
geterror(bp)
	register struct buf *bp;
{
	register int error = 0;

	if (bp->b_flags & B_ERROR)
		if ((error = bp->b_error) == 0)
			return (EIO);
	return (error);
}

/*
 * Return a count of buffers on the "locked" queue.
 */
int
count_lock_queue()
{
	register struct buf *bp;
	register int n = 0;

	for (bp = TAILQ_FIRST(&bufqueues[BQ_LOCKED]); bp;
	    bp = TAILQ_NEXT(bp, b_freelist))
		n++;
	return (n);
}

#ifdef DIAGNOSTIC
/*
 * Print out statistics on the current allocation of the buffer pool.
 * Can be enabled to print out on every ``sync'' by setting "syncprt"
 * in vfs_syscalls.c using sysctl.
 */
void
vfs_bufstats()
{
	int s, i, j, count;
	register struct buf *bp;
	register struct bqueues *dp;
	int counts[MAXBSIZE/CLBYTES+1];
	static char *bname[BQUEUES] = { "LOCKED", "LRU", "AGE", "EMPTY" };

	for (dp = bufqueues, i = 0; dp < &bufqueues[BQUEUES]; dp++, i++) {
		count = 0;
		for (j = 0; j <= MAXBSIZE/CLBYTES; j++)
			counts[j] = 0;
		s = splbio();
		for (bp = TAILQ_FIRST(dp); bp; bp = TAILQ_NEXT(bp, b_freelist)) {
			counts[bp->b_bufsize/CLBYTES]++;
			count++;
		}
		splx(s);
		printf("%s: total-%d", bname[i], count);
		for (j = 0; j <= MAXBSIZE/CLBYTES; j++)
			if (counts[j] != 0)
				printf(", %d-%d", j * CLBYTES, counts[j]);
		printf("\n");
	}
}
#endif /* DIAGNOSTIC */
