/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfs_bio.c	8.9 (Berkeley) 3/30/95
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/trace.h>
#include <sys/mount.h>
#include <sys/kernel.h>

#include <vm/include/vm.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsmount.h>
#include <nfs/nqnfs.h>
#include <nfs/nfsnode.h>

struct buf *nfs_getcacheblk(struct vnode *, daddr_t, int, struct proc *);
extern struct proc *nfs_iodwant[NFS_MAXASYNCDAEMON];
extern int nfs_numasync;
extern struct nfsstats nfsstats;

/*
 * Vnode op for read using bio
 * Any similarity to readip() is purely coincidental
 */
int
nfs_bioread(vp, uio, ioflag, cred)
	register struct vnode *vp;
	register struct uio *uio;
	int ioflag;
	struct ucred *cred;
{
	register struct nfsnode *np = VTONFS(vp);
	register int biosize, diff, i;
	struct buf *bp = 0, *rabp;
	struct vattr vattr;
	struct proc *p;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	daddr_t lbn, bn, bn2, rabn;
	caddr_t baddr;
	int got_buf = 0, nra, error = 0, n = 0, on = 0, not_readin;
	nfsquad_t tquad;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("nfs_read mode");
#endif
	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);
	p = uio->uio_procp;
	if ((nmp->nm_flag & (NFSMNT_NFSV3 | NFSMNT_GOTFSINFO)) == NFSMNT_NFSV3)
		(void) nfs_fsinfo(nmp, vp, cred, p);
	biosize = nmp->nm_rsize;
	/*
	 * For nfs, cache consistency can only be maintained approximately.
	 * Although RFC1094 does not specify the criteria, the following is
	 * believed to be compatible with the reference port.
	 * For nqnfs, full cache consistency is maintained within the loop.
	 * For nfs:
	 * If the file's modify time on the server has changed since the
	 * last read rpc or you have written to the file,
	 * you may have lost data cache consistency with the
	 * server, so flush all of the file's data out of the cache.
	 * Then force a getattr rpc to ensure that you have up to date
	 * attributes.
	 * NB: This implies that cache data can be read when up to
	 * NFS_ATTRTIMEO seconds out of date. If you find that you need current
	 * attributes this could be forced by setting n_attrstamp to 0 before
	 * the VOP_GETATTR() call.
	 */
	if ((nmp->nm_flag & NFSMNT_NQNFS) == 0 && vp->v_type != VLNK) {
		if (np->n_flag & NMODIFIED) {
			if (vp->v_type != VREG) {
				if (vp->v_type != VDIR)
					panic("nfs: bioread, not dir");
				nfs_invaldir(vp);
				error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
				if (error)
					return (error);
			}
			np->n_attrstamp = 0;
			error = VOP_GETATTR(vp, &vattr, cred, p);
			if (error)
				return (error);
			np->n_mtime = vattr.va_mtime.tv_sec;
		} else {
			error = VOP_GETATTR(vp, &vattr, cred, p);
			if (error)
				return (error);
			if (np->n_mtime != vattr.va_mtime.tv_sec) {
				if (vp->v_type == VDIR)
					nfs_invaldir(vp);
				error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
				if (error)
					return (error);
				np->n_mtime = vattr.va_mtime.tv_sec;
			}
		}
	}
	do {

		/*
		 * Get a valid lease. If cached data is stale, flush it.
		 */
		if (nmp->nm_flag & NFSMNT_NQNFS) {
			if (NQNFS_CKINVALID(vp, np, ND_READ)) {
				do {
					error = nqnfs_getlease(vp, ND_READ, cred, p);
				} while (error == NQNFS_EXPIRED);
				if (error)
					return (error);
				if (np->n_lrev != np->n_brev || (np->n_flag & NQNFSNONCACHE)
						|| ((np->n_flag & NMODIFIED) && vp->v_type == VDIR)) {
					if (vp->v_type == VDIR)
						nfs_invaldir(vp);
					error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
					if (error)
						return (error);
					np->n_brev = np->n_lrev;
				}
			} else if (vp->v_type == VDIR && (np->n_flag & NMODIFIED)) {
				nfs_invaldir(vp);
				error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
				if (error)
					return (error);
			}
		}
		if (np->n_flag & NQNFSNONCACHE) {
			switch (vp->v_type) {
			case VREG:
				return (nfs_readrpc(vp, uio, cred));
			case VLNK:
				return (nfs_readlinkrpc(vp, uio, cred));
			case VDIR:
				break;
			default:
				printf(" NQNFSNONCACHE: type %x unexpected\n", vp->v_type);
			};
		}
		baddr = (caddr_t) 0;
		switch (vp->v_type) {
		case VREG:
			nfsstats.biocache_reads++;
			lbn = uio->uio_offset / biosize;
			on = uio->uio_offset & (biosize - 1);
			bn = lbn * (biosize / DEV_BSIZE);
			not_readin = 1;

			/*
			 * Start the read ahead(s), as required.
			 */
			if (nfs_numasync > 0 && nmp->nm_readahead > 0) {
				for (nra = 0;
						nra < nmp->nm_readahead
								&& (lbn + 1 + nra) * biosize < np->n_size;
						nra++) {
					rabn = (lbn + 1 + nra) * (biosize / DEV_BSIZE);
					if (!incore(vp, rabn)) {
						rabp = nfs_getcacheblk(vp, rabn, biosize, p);
						if (!rabp)
							return (EINTR);
						if ((rabp->b_flags & (B_DELWRI | B_DONE)) == 0) {
							rabp->b_flags |= (B_READ | B_ASYNC);
							if (nfs_asyncio(rabp, cred)) {
								rabp->b_flags |= B_INVAL;
								brelse(rabp);
							}
						} else
							brelse(rabp);
					}
				}
			}

			/*
			 * If the block is in the cache and has the required data
			 * in a valid region, just copy it out.
			 * Otherwise, get the block and write back/read in,
			 * as required.
			 */
			if ((bp = incore(vp, bn))
					&& (bp->b_flags & (B_BUSY | B_WRITEINPROG))
							== (B_BUSY | B_WRITEINPROG))
				got_buf = 0;
			else {
again:
			bp = nfs_getcacheblk(vp, bn, biosize, p);
				if (!bp)
					return (EINTR);
				got_buf = 1;
				if ((bp->b_flags & (B_DONE | B_DELWRI)) == 0) {
					bp->b_flags |= B_READ;
					not_readin = 0;
					error = nfs_doio(bp, cred, p);
					if (error) {
						brelse(bp);
						return (error);
					}
				}
			}
			n = min((unsigned) (biosize - on), uio->uio_resid);
			diff = np->n_size - uio->uio_offset;
			if (diff < n)
				n = diff;
			if (not_readin && n > 0) {
				if (on < bp->b_validoff || (on + n) > bp->b_validend) {
					if (!got_buf) {
						bp = nfs_getcacheblk(vp, bn, biosize, p);
						if (!bp)
							return (EINTR);
						got_buf = 1;
					}
					bp->b_flags |= B_INVAFTERWRITE;
					if (bp->b_dirtyend > 0) {
						if ((bp->b_flags & B_DELWRI) == 0)
							panic("nfsbioread");
						if (VOP_BWRITE(bp) == EINTR)
							return (EINTR);
					} else
						brelse(bp);
					goto again;
				}
			}
			vp->v_lastr = lbn;
			diff = (on >= bp->b_validend) ? 0 : (bp->b_validend - on);
			if (diff < n)
				n = diff;
			break;
		case VLNK:
			nfsstats.biocache_readlinks++;
			bp = nfs_getcacheblk(vp, (daddr_t) 0, NFS_MAXPATHLEN, p);
			if (!bp)
				return (EINTR);
			if ((bp->b_flags & B_DONE) == 0) {
				bp->b_flags |= B_READ;
				error = nfs_doio(bp, cred, p);
				if (error) {
					brelse(bp);
					return (error);
				}
			}
			n = min(uio->uio_resid, NFS_MAXPATHLEN - bp->b_resid);
			got_buf = 1;
			on = 0;
			break;
		case VDIR:
			nfsstats.biocache_readdirs++;
			lbn = uio->uio_offset / NFS_DIRBLKSIZ;
			on = uio->uio_offset & (NFS_DIRBLKSIZ - 1);
			bp = nfs_getcacheblk(vp, lbn, NFS_DIRBLKSIZ, p);
			if (!bp)
				return (EINTR);
			if ((bp->b_flags & B_DONE) == 0) {
				bp->b_flags |= B_READ;
				error = nfs_doio(bp, cred, p);
				if (error) {
					brelse(bp);
					while (error == NFSERR_BAD_COOKIE) {
						nfs_invaldir(vp);
						error = nfs_vinvalbuf(vp, 0, cred, p, 1);
						/*
						 * Yuck! The directory has been modified on the
						 * server. The only way to get the block is by
						 * reading from the beginning to get all the
						 * offset cookies.
						 */
						for (i = 0; i <= lbn && !error; i++) {
							bp = nfs_getcacheblk(vp, i, NFS_DIRBLKSIZ, p);
							if (!bp)
								return (EINTR);
							if ((bp->b_flags & B_DONE) == 0) {
								bp->b_flags |= B_READ;
								error = nfs_doio(bp, cred, p);
								if (error)
									brelse(bp);
							}
						}
					}
					if (error)
						return (error);
				}
			}

			/*
			 * If not eof and read aheads are enabled, start one.
			 * (You need the current block first, so that you have the
			 *  directory offset cookie of the next block.)
			 */
			if (nfs_numasync > 0 && nmp->nm_readahead > 0
					&& (np->n_direofoffset == 0
							|| (lbn + 1) * NFS_DIRBLKSIZ < np->n_direofoffset)
					&& !(np->n_flag & NQNFSNONCACHE) && !incore(vp, lbn + 1)) {
				rabp = nfs_getcacheblk(vp, lbn + 1, NFS_DIRBLKSIZ, p);
				if (rabp) {
					if ((rabp->b_flags & (B_DONE | B_DELWRI)) == 0) {
						rabp->b_flags |= (B_READ | B_ASYNC);
						if (nfs_asyncio(rabp, cred)) {
							rabp->b_flags |= B_INVAL;
							brelse(rabp);
						}
					} else
						brelse(rabp);
				}
			}
			n = min(uio->uio_resid, NFS_DIRBLKSIZ - bp->b_resid - on);
			got_buf = 1;
			break;
		default:
			printf(" nfsbioread: type %x unexpected\n", vp->v_type);
			break;
		};

		if (n > 0) {
			if (!baddr)
				baddr = bp->b_data;
			error = uiomove(baddr + on, (int) n, uio);
		}
		switch (vp->v_type) {
		case VREG:
			break;
		case VLNK:
			n = 0;
			break;
		case VDIR:
			if (np->n_flag & NQNFSNONCACHE)
				bp->b_flags |= B_INVAL;
			break;
		default:
			printf(" nfsbioread: type %x unexpected\n", vp->v_type);
		}
		if (got_buf)
			brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n > 0);
	return (error);
}

/*
 * Vnode op for write using bio
 */
int
nfs_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	register int biosize;
	register struct uio *uio = ap->a_uio;
	struct proc *p = uio->uio_procp;
	register struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	register struct ucred *cred = ap->a_cred;
	int ioflag = ap->a_ioflag;
	struct buf *bp;
	struct vattr vattr;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	daddr_t lbn, bn;
	int n, on, error = 0, iomode, must_commit;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_WRITE)
		panic("nfs_write mode");
	if (uio->uio_segflg == UIO_USERSPACE && uio->uio_procp != curproc)
		panic("nfs_write proc");
#endif
	if (vp->v_type != VREG)
		return (EIO);
	if (np->n_flag & NWRITEERR) {
		np->n_flag &= ~NWRITEERR;
		return (np->n_error);
	}
	if ((nmp->nm_flag & (NFSMNT_NFSV3 | NFSMNT_GOTFSINFO)) == NFSMNT_NFSV3)
		(void)nfs_fsinfo(nmp, vp, cred, p);
	if (ioflag & (IO_APPEND | IO_SYNC)) {
		if (np->n_flag & NMODIFIED) {
			np->n_attrstamp = 0;
			error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
			if (error)
				return (error);
		}
		if (ioflag & IO_APPEND) {
			np->n_attrstamp = 0;
			error = VOP_GETATTR(vp, &vattr, cred, p);
			if (error)
				return (error);
			uio->uio_offset = np->n_size;
		}
	}
	if (uio->uio_offset < 0)
		return (EINVAL);
	if (uio->uio_resid == 0)
		return (0);
	/*
	 * Maybe this should be above the vnode op call, but so long as
	 * file servers have no limits, i don't think it matters
	 */
	if (p && uio->uio_offset + uio->uio_resid >
	      p->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
		psignal(p, SIGXFSZ);
		return (EFBIG);
	}
	/*
	 * I use nm_rsize, not nm_wsize so that all buffer cache blocks
	 * will be the same size within a filesystem. nfs_writerpc will
	 * still use nm_wsize when sizing the rpc's.
	 */
	biosize = nmp->nm_rsize;
	do {

		/*
		 * XXX make sure we aren't cached in the VM page cache
		 */
		(void)vnode_pager_uncache(vp);

		/*
		 * Check for a valid write lease.
		 */
		if ((nmp->nm_flag & NFSMNT_NQNFS) && NQNFS_CKINVALID(vp, np, ND_WRITE)) {
			do {
				error = nqnfs_getlease(vp, ND_WRITE, cred, p);
			} while (error == NQNFS_EXPIRED);
			if (error)
				return (error);
			if (np->n_lrev != np->n_brev || (np->n_flag & NQNFSNONCACHE)) {
				error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
				if (error)
					return (error);
				np->n_brev = np->n_lrev;
			}
		}
		if ((np->n_flag & NQNFSNONCACHE) && uio->uio_iovcnt == 1) {
			iomode = NFSV3WRITE_FILESYNC;
			error = nfs_writerpc(vp, uio, cred, &iomode, &must_commit);
			if (must_commit)
				nfs_clearcommit(vp->v_mount);
			return (error);
		}
		nfsstats.biocache_writes++;
		lbn = uio->uio_offset / biosize;
		on = uio->uio_offset & (biosize - 1);
		n = min((unsigned) (biosize - on), uio->uio_resid);
		bn = lbn * (biosize / DEV_BSIZE);
again:
		bp = nfs_getcacheblk(vp, bn, biosize, p);
		if (!bp)
			return (EINTR);
		if (bp->b_wcred == NOCRED) {
			crhold(cred);
			bp->b_wcred = cred;
		}
		np->n_flag |= NMODIFIED;
		if (uio->uio_offset + n > np->n_size) {
			np->n_size = uio->uio_offset + n;
			vnode_pager_setsize(vp, (u_long) np->n_size);
		}

		/*
		 * If the new write will leave a contiguous dirty
		 * area, just update the b_dirtyoff and b_dirtyend,
		 * otherwise force a write rpc of the old dirty area.
		 */
		if (bp->b_dirtyend > 0
				&& (on > bp->b_dirtyend || (on + n) < bp->b_dirtyoff)) {
			bp->b_proc = p;
			if (VOP_BWRITE(bp) == EINTR)
				return (EINTR);
			goto again;
		}

		/*
		 * Check for valid write lease and get one as required.
		 * In case getblk() and/or bwrite() delayed us.
		 */
		if ((nmp->nm_flag & NFSMNT_NQNFS) &&
		    NQNFS_CKINVALID(vp, np, ND_WRITE)) {
			do {
				error = nqnfs_getlease(vp, ND_WRITE, cred, p);
			} while (error == NQNFS_EXPIRED);
			if (error) {
				brelse(bp);
				return (error);
			}
			if (np->n_lrev != np->n_brev ||
			    (np->n_flag & NQNFSNONCACHE)) {
				brelse(bp);
				error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
				if (error)
					return (error);
				np->n_brev = np->n_lrev;
				goto again;
			}
		}
		error = uiomove((char *)bp->b_data + on, n, uio);
		if (error) {
			bp->b_flags |= B_ERROR;
			brelse(bp);
			return (error);
		}
		if (bp->b_dirtyend > 0) {
			bp->b_dirtyoff = min(on, bp->b_dirtyoff);
			bp->b_dirtyend = max((on + n), bp->b_dirtyend);
		} else {
			bp->b_dirtyoff = on;
			bp->b_dirtyend = on + n;
		}
		if (bp->b_validend == 0 || bp->b_validend < bp->b_dirtyoff ||
		    bp->b_validoff > bp->b_dirtyend) {
			bp->b_validoff = bp->b_dirtyoff;
			bp->b_validend = bp->b_dirtyend;
		} else {
			bp->b_validoff = min(bp->b_validoff, bp->b_dirtyoff);
			bp->b_validend = max(bp->b_validend, bp->b_dirtyend);
		}
		/*
		 * If the lease is non-cachable or IO_SYNC do bwrite().
		 */
		if ((np->n_flag & NQNFSNONCACHE) || (ioflag & IO_SYNC)) {
			bp->b_proc = p;
			error = VOP_BWRITE(bp);
			if (error)
				return (error);
			if (np->n_flag & NQNFSNONCACHE) {
				error = nfs_vinvalbuf(vp, V_SAVE, cred, p, 1);
				if (error)
					return (error);
			}
		} else if ((n + on) == biosize &&
			(nmp->nm_flag & NFSMNT_NQNFS) == 0) {
			bp->b_proc = (struct proc *)0;
			bp->b_flags |= B_ASYNC;
			(void)nfs_writebp(bp, 0);
		} else
			bdwrite(bp);
	} while (uio->uio_resid > 0 && n > 0);
	return (0);
}

/*
 * Get an nfs cache block.
 * Allocate a new one if the block isn't currently in the cache
 * and return the block marked busy. If the calling process is
 * interrupted by a signal for an interruptible mount point, return
 * NULL.
 */
struct buf *
nfs_getcacheblk(vp, bn, size, p)
	struct vnode *vp;
	daddr_t bn;
	int size;
	struct proc *p;
{
	register struct buf *bp;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);

	if (nmp->nm_flag & NFSMNT_INT) {
		bp = getblk(vp, bn, size, PCATCH, 0);
		while (bp == (struct buf *)0) {
			if (nfs_sigintr(nmp, (struct nfsreq *)0, p))
				return ((struct buf *)0);
			bp = getblk(vp, bn, size, 0, 2 * hz);
		}
	} else
		bp = getblk(vp, bn, size, 0, 0);
	return (bp);
}

/*
 * Flush and invalidate all dirty buffers. If another process is already
 * doing the flush, just wait for completion.
 */
int
nfs_vinvalbuf(vp, flags, cred, p, intrflg)
	struct vnode *vp;
	int flags;
	struct ucred *cred;
	struct proc *p;
	int intrflg;
{
	register struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, slpflag, slptimeo;

	if ((nmp->nm_flag & NFSMNT_INT) == 0)
		intrflg = 0;
	if (intrflg) {
		slpflag = PCATCH;
		slptimeo = 2 * hz;
	} else {
		slpflag = 0;
		slptimeo = 0;
	}
	/*
	 * First wait for any other process doing a flush to complete.
	 */
	while (np->n_flag & NFLUSHINPROG) {
		np->n_flag |= NFLUSHWANT;
		error = tsleep((caddr_t)&np->n_flag, PRIBIO + 2, "nfsvinval",
			slptimeo);
		if (error && intrflg && nfs_sigintr(nmp, (struct nfsreq *)0, p))
			return (EINTR);
	}

	/*
	 * Now, flush as required.
	 */
	np->n_flag |= NFLUSHINPROG;
	error = vinvalbuf(vp, flags, cred, p, slpflag, 0);
	while (error) {
		if (intrflg && nfs_sigintr(nmp, (struct nfsreq *)0, p)) {
			np->n_flag &= ~NFLUSHINPROG;
			if (np->n_flag & NFLUSHWANT) {
				np->n_flag &= ~NFLUSHWANT;
				wakeup((caddr_t)&np->n_flag);
			}
			return (EINTR);
		}
		error = vinvalbuf(vp, flags, cred, p, 0, slptimeo);
	}
	np->n_flag &= ~(NMODIFIED | NFLUSHINPROG);
	if (np->n_flag & NFLUSHWANT) {
		np->n_flag &= ~NFLUSHWANT;
		wakeup((caddr_t)&np->n_flag);
	}
	return (0);
}

/*
 * Initiate asynchronous I/O. Return an error if no nfsiods are available.
 * This is mainly to avoid queueing async I/O requests when the nfsiods
 * are all hung on a dead server.
 */
int
nfs_asyncio(bp, cred)
	register struct buf *bp;
	struct ucred *cred;
{
	register int i;

	if (nfs_numasync == 0)
		return (EIO);
	for (i = 0; i < NFS_MAXASYNCDAEMON; i++)
		if (nfs_iodwant[i]) {
			if (bp->b_flags & B_READ) {
				if (bp->b_rcred == NOCRED && cred != NOCRED) {
					crhold(cred);
					bp->b_rcred = cred;
				}
			} else {
				bp->b_flags |= B_WRITEINPROG;
				if (bp->b_wcred == NOCRED && cred != NOCRED) {
					crhold(cred);
					bp->b_wcred = cred;
				}
			}

			TAILQ_INSERT_TAIL(&nfs_bufq, bp, b_freelist);
			nfs_iodwant[i] = (struct proc*) 0;
			wakeup((caddr_t) & nfs_iodwant[i]);
			return (0);
		}

	/*
	 * If it is a read or a write already marked B_WRITEINPROG or B_NOCACHE
	 * return EIO so the process will call nfs_doio() and do it
	 * synchronously.
	 */
	if (bp->b_flags & (B_READ | B_WRITEINPROG | B_NOCACHE))
		return (EIO);

	/*
	 * Just turn the async write into a delayed write, instead of
	 * doing in synchronously. Hopefully, at least one of the nfsiods
	 * is currently doing a write for this file and will pick up the
	 * delayed writes before going back to sleep.
	 */
	bp->b_flags |= B_DELWRI;
	reassignbuf(bp, bp->b_vp);
	biodone(bp);
	return (0);
}

/*
 * Do an I/O operation to/from a cache block. This may be called
 * synchronously or from an nfsiod.
 */
int
nfs_doio(bp, cr, p)
	register struct buf *bp;
	struct ucred *cr;
	struct proc *p;
{
	register struct uio *uiop;
	register struct vnode *vp;
	struct nfsnode *np;
	struct nfsmount *nmp;
	int error = 0, diff, len, iomode, must_commit = 0;
	struct uio uio;
	struct iovec io;
	nfsquad_t tquad;

	vp = bp->b_vp;
	np = VTONFS(vp);
	nmp = VFSTONFS(vp->v_mount);
	uiop = &uio;
	uiop->uio_iov = &io;
	uiop->uio_iovcnt = 1;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_procp = p;

	/*
	 * Historically, paging was done with physio, but no more.
	 */
	if (bp->b_flags & B_PHYS) {
		/*
		 * ...though reading /dev/drum still gets us here.
		 */
		io.iov_len = uiop->uio_resid = bp->b_bcount;
		/* mapping was done by vmapbuf() */
		io.iov_base = bp->b_data;
		uiop->uio_offset = ((off_t) bp->b_blkno) * DEV_BSIZE;
		if (bp->b_flags & B_READ) {
			uiop->uio_rw = UIO_READ;
			nfsstats.read_physios++;
			error = nfs_readrpc(vp, uiop, cr);
		} else
			panic("physio write");
		if (error) {
			bp->b_flags |= B_ERROR;
			bp->b_error = error;
		}
	} else if (bp->b_flags & B_READ) {
		io.iov_len = uiop->uio_resid = bp->b_bcount;
		io.iov_base = bp->b_data;
		uiop->uio_rw = UIO_READ;
		switch (vp->v_type) {
		case VREG:
			uiop->uio_offset = ((off_t) bp->b_blkno) * DEV_BSIZE;
			nfsstats.read_bios++;
			error = nfs_readrpc(vp, uiop, cr);
			if (!error) {
				bp->b_validoff = 0;
				if (uiop->uio_resid) {
					/*
					 * If len > 0, there is a hole in the file and
					 * no writes after the hole have been pushed to
					 * the server yet.
					 * Just zero fill the rest of the valid area.
					 */
					diff = bp->b_bcount - uiop->uio_resid;
					len = np->n_size
							- (((u_quad_t) bp->b_blkno) * DEV_BSIZE + diff);
					if (len > 0) {
						len = min(len, uiop->uio_resid);
						bzero((char*) bp->b_data + diff, len);
						bp->b_validend = diff + len;
					} else
						bp->b_validend = diff;
				} else
					bp->b_validend = bp->b_bcount;
			}
			if (p && (vp->v_flag & VTEXT)
					&& (((nmp->nm_flag & NFSMNT_NQNFS)
							&& NQNFS_CKINVALID(vp, np, ND_READ)
							&& np->n_lrev != np->n_brev)
							|| (!(nmp->nm_flag & NFSMNT_NQNFS)
									&& np->n_mtime
											!= np->n_vattr.va_mtime.ts_sec))) {
				uprintf("Process killed due to text file modification\n");
				psignal(p, SIGKILL);
				p->p_flag |= P_NOSWAP;
			}
			break;
		case VLNK:
			uiop->uio_offset = (off_t) 0;
			nfsstats.readlink_bios++;
			error = nfs_readlinkrpc(vp, uiop, cr);
			break;
		case VDIR:
			nfsstats.readdir_bios++;
			uiop->uio_offset = ((u_quad_t) bp->b_lblkno) * NFS_DIRBLKSIZ;
			if (nmp->nm_flag & NFSMNT_RDIRPLUS) {
				error = nfs_readdirplusrpc(vp, uiop, cr);
				if (error == NFSERR_NOTSUPP)
					nmp->nm_flag &= ~NFSMNT_RDIRPLUS;
			}
			if ((nmp->nm_flag & NFSMNT_RDIRPLUS) == 0)
				error = nfs_readdirrpc(vp, uiop, cr);
			break;
		default:
			printf("nfs_doio:  type %x unexpected\n", vp->v_type);
			break;
		};
		if (error) {
			bp->b_flags |= B_ERROR;
			bp->b_error = error;
		}
	} else {
		io.iov_len = uiop->uio_resid = bp->b_dirtyend - bp->b_dirtyoff;
		uiop->uio_offset = ((off_t) bp->b_blkno) * DEV_BSIZE + bp->b_dirtyoff;
		io.iov_base = (char*) bp->b_data + bp->b_dirtyoff;
		uiop->uio_rw = UIO_WRITE;
		nfsstats.write_bios++;
		if ((bp->b_flags & (B_ASYNC | B_NEEDCOMMIT | B_NOCACHE)) == B_ASYNC)
			iomode = NFSV3WRITE_UNSTABLE;
		else
			iomode = NFSV3WRITE_FILESYNC;
		bp->b_flags |= B_WRITEINPROG;
		error = nfs_writerpc(vp, uiop, cr, &iomode, &must_commit);
		if (!error && iomode == NFSV3WRITE_UNSTABLE)
			bp->b_flags |= B_NEEDCOMMIT;
		else
			bp->b_flags &= ~B_NEEDCOMMIT;
		bp->b_flags &= ~B_WRITEINPROG;

	    /*
	     * For an interrupted write, the buffer is still valid and the
	     * write hasn't been pushed to the server yet, so we can't set
	     * B_ERROR and report the interruption by setting B_EINTR. For
	     * the B_ASYNC case, B_EINTR is not relevant, so the rpc attempt
	     * is essentially a noop.
	     * For the case of a V3 write rpc not being committed to stable
	     * storage, the block is still dirty and requires either a commit
	     * rpc or another write rpc with iomode == NFSV3WRITE_FILESYNC
	     * before the block is reused. This is indicated by setting the
	     * B_DELWRI and B_NEEDCOMMIT flags.
	     */
		if (error == EINTR || (!error && (bp->b_flags & B_NEEDCOMMIT))) {
			bp->b_flags |= B_DELWRI;

			/*
			 * Since for the B_ASYNC case, nfs_bwrite() has reassigned the
			 * buffer to the clean list, we have to reassign it back to the
			 * dirty one. Ugh.
			 */
			if (bp->b_flags & B_ASYNC)
				reassignbuf(bp, vp);
			else
				bp->b_flags |= B_EINTR;
		} else {
			if (error) {
				bp->b_flags |= B_ERROR;
				bp->b_error = np->n_error = error;
				np->n_flag |= NWRITEERR;
			}
			bp->b_dirtyoff = bp->b_dirtyend = 0;
		}
	}
	bp->b_resid = uiop->uio_resid;
	if (must_commit)
		nfs_clearcommit(vp->v_mount);
	biodone(bp);
	return (error);
}
