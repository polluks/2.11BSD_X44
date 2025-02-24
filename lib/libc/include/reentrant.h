/*	$NetBSD: reentrant.h,v 1.10 2004/12/14 00:23:19 nathanw Exp $	*/

/*-
 * Copyright (c) 1997, 1998, 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by J.T. Conklin, by Nathan J. Williams, and by Jason R. Thorpe.
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

/*
 * Requirements:
 * 
 * 1. The thread safe mechanism should be lightweight so the library can
 *    be used by non-threaded applications without unreasonable overhead.
 * 
 * 2. There should be no dependency on a thread engine for non-threaded
 *    applications.
 * 
 * 3. There should be no dependency on any particular thread engine.
 * 
 * 4. The library should be able to be compiled without support for thread
 *    safety.
 * 
 * 
 * Rationale:
 * 
 * One approach for thread safety is to provide discrete versions of the
 * library: one thread safe, the other not.  The disadvantage of this is
 * that libc is rather large, and two copies of a library which are 99%+
 * identical is not an efficient use of resources.
 * 
 * Another approach is to provide a single thread safe library.  However,
 * it should not add significant run time or code size overhead to non-
 * threaded applications.
 * 
 * Since the NetBSD C library is used in other projects, it should be
 * easy to replace the mutual exclusion primitives with ones provided by
 * another system.  Similarly, it should also be easy to remove all
 * support for thread safety completely if the target environment does
 * not support threads.
 * 
 * 
 * Implementation Details:
 * 
 * The thread primitives used by the library (mutex_t, mutex_lock, etc.)
 * are macros which expand to the cooresponding primitives provided by
 * the thread engine or to nothing.  The latter is used so that code is
 * not unreasonably cluttered with #ifdefs when all thread safe support
 * is removed.
 * 
 * The thread macros can be directly mapped to the mutex primitives from
 * pthreads, however it should be reasonably easy to wrap another mutex
 * implementation so it presents a similar interface.
 * 
 * The thread functions operate by dispatching to symbols which are, by
 * default, weak-aliased to no-op functions in thread-stub/thread-stub.c
 * (some uses of thread operations are conditional on __isthreaded, but
 * not all of them are).
 *
 * When the thread library is linked in, it provides strong-alias versions
 * of those symbols which dispatch to its own real thread operations.
 *
 */

#ifdef _REENTRANT

/*
 * Abstract thread interface for thread-safe libraries.  These routines
 * will use stubs in libc if the application is not linked against the
 * pthread library, and the real function in the pthread library if it
 * is.
 */

#include <pthread.h>
#include <signal.h>

#define	mutex_t					pthread_mutex_t
#define	MUTEX_INITIALIZER		PTHREAD_MUTEX_INITIALIZER

#define	mutexattr_t				pthread_mutexattr_t

#define	MUTEX_TYPE_NORMAL		PTHREAD_MUTEX_NORMAL
#define	MUTEX_TYPE_ERRORCHECK	PTHREAD_MUTEX_ERRORCHECK
#define	MUTEX_TYPE_RECURSIVE	PTHREAD_MUTEX_RECURSIVE

#define	cond_t					pthread_cond_t
#define	COND_INITIALIZER		PTHREAD_COND_INITIALIZER

#define	condattr_t				pthread_condattr_t

#define	rwlock_t				pthread_rwlock_t
#define	RWLOCK_INITIALIZER		PTHREAD_RWLOCK_INITIALIZER

#define	rwlockattr_t			pthread_rwlockattr_t

#define	thread_key_t			pthread_key_t

#define	thr_t					pthread_t

#define	thrattr_t				pthread_attr_t

#define	once_t					pthread_once_t
#define	ONCE_INITIALIZER		PTHREAD_ONCE_INIT

#ifndef __LIBC_THREAD_STUBS

__BEGIN_DECLS
int	__libc_mutex_init(mutex_t *, const mutexattr_t *);
int	__libc_mutex_lock(mutex_t *);
int	__libc_mutex_trylock(mutex_t *);
int	__libc_mutex_unlock(mutex_t *);
int	__libc_mutex_destroy(mutex_t *);

int	__libc_mutexattr_init(mutexattr_t *);
int	__libc_mutexattr_settype(mutexattr_t *, int);
int	__libc_mutexattr_destroy(mutexattr_t *);
__END_DECLS

#define	mutex_init(m, a)		__libc_mutex_init((m), (a))
#define	mutex_lock(m)			__libc_mutex_lock((m))
#define	mutex_trylock(m)		__libc_mutex_trylock((m))
#define	mutex_unlock(m)			__libc_mutex_unlock((m))
#define	mutex_destroy(m)		__libc_mutex_destroy((m))

#define	mutexattr_init(ma)		__libc_mutexattr_init((ma))
#define	mutexattr_settype(ma, t) __libc_mutexattr_settype((ma), (t))
#define	mutexattr_destroy(ma)	__libc_mutexattr_destroy((ma))

__BEGIN_DECLS
int	__libc_cond_init(cond_t *, const condattr_t *);
int	__libc_cond_signal(cond_t *);
int	__libc_cond_broadcast(cond_t *);
int	__libc_cond_wait(cond_t *, mutex_t *);
int	__libc_cond_timedwait(cond_t *, mutex_t *, const struct timespec *);
int	__libc_cond_destroy(cond_t *);
__END_DECLS

#define	cond_init(c, t, a)     	__libc_cond_init((c), (a))
#define	cond_signal(c)			__libc_cond_signal((c))
#define	cond_broadcast(c)		__libc_cond_broadcast((c))
#define	cond_wait(c, m)			__libc_cond_wait((c), (m))
#define	cond_timedwait(c, m, t)	__libc_cond_timedwait((c), (m), (t))
#define	cond_destroy(c)			__libc_cond_destroy((c))

__BEGIN_DECLS
int	__libc_rwlock_init(rwlock_t *, const rwlockattr_t *);
int	__libc_rwlock_rdlock(rwlock_t *);
int	__libc_rwlock_wrlock(rwlock_t *);
int	__libc_rwlock_tryrdlock(rwlock_t *);
int	__libc_rwlock_trywrlock(rwlock_t *);
int	__libc_rwlock_unlock(rwlock_t *);
int	__libc_rwlock_destroy(rwlock_t *);
__END_DECLS

#define	rwlock_init(l, a)		__libc_rwlock_init((l), (a))
#define	rwlock_rdlock(l)		__libc_rwlock_rdlock((l))
#define	rwlock_wrlock(l)		__libc_rwlock_wrlock((l))
#define	rwlock_tryrdlock(l)		__libc_rwlock_tryrdlock((l))
#define	rwlock_trywrlock(l)		__libc_rwlock_trywrlock((l))
#define	rwlock_unlock(l)		__libc_rwlock_unlock((l))
#define	rwlock_destroy(l)		__libc_rwlock_destroy((l))

__BEGIN_DECLS
int	__libc_thr_keycreate(thread_key_t *, void (*)(void *));
int	__libc_thr_setspecific(thread_key_t, const void *);
void *__libc_thr_getspecific(thread_key_t);
int	__libc_thr_keydelete(thread_key_t);
__END_DECLS

#define	thr_keycreate(k, d)		__libc_thr_keycreate((k), (d))
#define	thr_setspecific(k, p)	__libc_thr_setspecific((k), (p))
#define	thr_getspecific(k)		__libc_thr_getspecific((k))
#define	thr_keydelete(k)		__libc_thr_keydelete((k))

__BEGIN_DECLS
int	__libc_thr_once(once_t *, void (*)(void));
int	__libc_thr_sigsetmask(int, const sigset_t *, sigset_t *);
thr_t __libc_thr_self(void);
void __libc_thr_yield(void);
void __libc_thr_create(thr_t *, const thrattr_t *, void *(*)(void *), void *);
void __libc_thr_exit(void *) __attribute__((__noreturn__));
int	*__libc_thr_errno(void);
int	__libc_thr_setcancelstate(int, int *);

extern int __isthreaded;
__END_DECLS

#endif /* __LIBC_THREAD_STUBS */


__BEGIN_DECLS
int 	__libc_accept(int, struct sockaddr *, socklen_t *);
int 	__libc_atfork(void (*)(void), void (*)(void), void (*)(void));
int		__libc_clock_gettime(clockid_t clock_id, struct timespec *tp);
int		__libc_clock_settime(clockid_t clock_id, const struct timespec *tp);
int 	__libc_close(int);
int 	__libc_connect(int, const struct sockaddr *, socklen_t);
int 	__libc_execve(const char *path, char *const *, char *const *);
int 	__libc_fcntl(int, int, va_list);
pid_t	__libc_fork(void);
int 	__libc_fsync(int);
int 	__libc_fsync_range(int, int, off_t, off_t);
int		__libc_getitimer(unsigned int, struct itimerval *);
int 	__libc_ksem_close(semid_t id);
int 	__libc_ksem_destroy(semid_t id);
int 	__libc_ksem_getvalue(semid_t id, unsigned int *value);
int 	__libc_ksem_init(unsigned int value, semid_t *idp);
int 	__libc_ksem_open(const char *name, int oflag, mode_t mode, unsigned int value, semid_t *idp);
int 	__libc_ksem_post(semid_t id);
int 	__libc_ksem_trywait(semid_t id);
int 	__libc_ksem_unlink(const char *name);
int 	__libc_ksem_wait(semid_t id);
ssize_t __libc_msgrcv(int, void *, size_t, long, int);
int 	__libc_msgsnd(int, const void *, size_t, int);
int 	__libc_msync(void *, size_t, int);
int		__libc_nanosleep(const struct timespec *, struct timespec *);
int 	__libc_open(const char *, int, va_list);
int 	__libc_poll(struct pollfd *, nfds_t, int);
ssize_t __libc_pread(int, void *, size_t, off_t);
ssize_t __libc_pwrite(int, const void *, size_t, off_t);
int 	__libc_read(int, void *, size_t);
int 	__libc_readv(int, const struct iovec *, int);
int 	__libc_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int	__libc_setitimer(unsigned int, struct itimerval *, struct itimerval *);
int 	__libc_sigaction(int, struct sigaction *, struct sigaction *);
int 	__libc_sigsuspend(const sigset_t *);
int 	__libc_sigprocmask(int, sigset_t *, sigset_t *);
int 	__libc_sigtimedwait(const sigset_t *, int *, struct timespec *);
int		__libc_wait4(pid_t, int *, int, struct rusage *);
int 	__libc_write(int, const void *, size_t);
int 	__libc_writev(int, const struct iovec *, int);
__END_DECLS

#define	thr_accept(s, addr, addrlen)					__libc_accept(s, addr, addrlen)
#define	thr_atfork(prepare, parent, child)				__libc_atfork(prepare, parent, child)
#define	thr_clock_gettime(clock_id, tp)					__libc_clock_gettime(clock_id, tp)
#define	thr_clock_settime(clock_id, tp)					__libc_clock_settime(clock_id, tp)
#define	thr_close(d)									__libc_close(d)
#define	thr_connect(s, addr, namelen)					__libc_connect(s, addr, namelen)
#define	thr_execve(fname, argp, envp)					__libc_execve(fname, argp, envp)
#define	thr_fcntl(fd, cmd, ap)							__libc_fcntl(fd, cmd, ap)
#define thr_fork()										__libc_fork()
#define	thr_fsync(d)									__libc_fsync(d)
#define	thr_fsync_range(d, f, s, e)						__libc_fsync_range(d, f, s, e)
#define thr_getitimer(which, itv)						__libc_getitimer(which, itv)
#define thr_ksem_close(id)								__libc_ksem_close(id)
#define thr_ksem_destroy(id)							__libc_ksem_destroy(id)
#define thr_ksem_getvalue(id, value)					__libc_ksem_getvalue(id, value)
#define thr_ksem_init(value, idp)						__libc_ksem_init(value, idp)
#define thr_ksem_open(name, oflag, mode, value, idp)	__libc_ksem_open(name, oflag, mode, value, idp)
#define thr_ksem_post(id) 								__libc_ksem_post(id)
#define thr_ksem_trywait(id)							__libc_ksem_trywait(id)
#define thr_ksem_unlink(name) 							__libc_ksem_unlink(name)
#define thr_ksem_wait(id) 								__libc_ksem_wait(id)
#define	thr_msgrcv(msgid, msgp, msgsz, msgtyp, msgflg) 	__libc_msgrcv(msgid, msgp, msgsz, msgtyp, msgflg)
#define	thr_msgsnd(msgid, msgp, msgsz, msgflg)			__libc_msgsnd(msgid, msgp, msgsz, msgflg)
#define	thr_msync(addr, len, flags)						__libc_msync(addr, len, flags)
#define thr_nanosleep(rqtp, rmtp)						__libc_nanosleep(rqtp, rmtp)
#define	thr_open(path, flags, ap)						__libc_open(path, flags, ap)
#define	thr_poll(fds, nfds, timeout)					__libc_poll(fds, nfds, timeout)
#define	thr_pread(d, buf, nbytes, offset)				__libc_pread(d, buf, nbytes, offset)
#define	thr_pwrite(d, buf, nbytes, offset)				__libc_pwrite(d, buf, nbytes, offset)
#define	thr_read(d, buf, nbytes)						__libc_read(d, buf, nbytes)
#define	thr_readv(d, iov, iovcnt)						__libc_readv(d, iov, iovcnt)
#define	thr_select(nfds, readfds, writefds, exceptfds, timeout) __libc_select(nfds, readfds, writefds, exceptfds, timeout)
#define thr_setitimer(which, itv, oitv)					__libc_setitimer(which, itv, oitv)
#define	thr_sigaction(sig, act, oact) 					__libc_sigaction(sig, act, oact)
#define	thr_sigmask(how, set, oset) 					__libc_sigprocmask(how, set, oset)
#define	thr_sigsuspend(sigmask) 						__libc_sigsuspend(sigmask)
#define	thr_timedwait(set, signo, timeout) 				__libc_sigtimedwait(set, signo, timeout)
#define	thr_wait4(wpid, status, options, rusage) 		__libc_wait4(wpid, status, options, rusage)
#define	thr_write(d, buf, nbytes) 						__libc_write(d, buf, nbytes)
#define	thr_writev(d, iov, iovcnt) 						__libc_writev(d, iov, iovcnt)

#define	FLOCKFILE(fp)			__flockfile_internal(fp, 1)
#define	FUNLOCKFILE(fp)			__funlockfile_internal(fp, 1)

#else /* _REENTRANT */

#define	mutex_init(m, a)
#define	mutex_lock(m)
#define	mutex_trylock(m)
#define	mutex_unlock(m)
#define	mutex_destroy(m)

#define	cond_init(c, t, a)
#define	cond_signal(c)
#define	cond_broadcast(c)
#define	cond_wait(c, m)
#define	cond_timedwait(c, m, t)
#define	cond_destroy(c)

#define	rwlock_init(l, a)
#define	rwlock_rdlock(l)
#define	rwlock_wrlock(l)
#define	rwlock_tryrdlock(l)
#define	rwlock_trywrlock(l)
#define	rwlock_unlock(l)
#define	rwlock_destroy(l)

#define	thr_keycreate(k, d)
#define	thr_setspecific(k, p)
#define	thr_getspecific(k)
#define	thr_keydelete(k)

#define	thr_once(o, f)
#define	thr_sigsetmask(f, n, o)
#define	thr_self()
#define	thr_errno()

#define	thr_accept(s, addr, addrlen)
#define	thr_atfork(prepare, parent, child)
#define	thr_clock_gettime(clock_id, tp)
#define	thr_clock_settime(clock_id, tp)
#define	thr_close(d)
#define	thr_connect(s, addr, namelen)
#define	thr_execve(fname, argp, envp)
#define	thr_fcntl(fd, cmd, ap)
#define thr_fork()
#define	thr_fsync(d)
#define	thr_fsync_range(d, f, s, e)
#define thr_getitimer(which, itv)
#define thr_ksem_close(id)
#define thr_ksem_destroy(id)
#define thr_ksem_getvalue(id, value)
#define thr_ksem_init(value, idp)
#define thr_ksem_open(name, oflag, mode, value, idp)
#define thr_ksem_post(id)
#define thr_ksem_trywait(id)
#define thr_ksem_unlink(name)
#define thr_ksem_wait(id)
#define	thr_msgrcv(msgid, msgp, msgsz, msgtyp, msgflg)
#define	thr_msgsnd(msgid, msgp, msgsz, msgflg)
#define	thr_msync(addr, len, flags)
#define thr_nanosleep(rqtp, rmtp)
#define	thr_open(path, flags, ap)
#define	thr_poll(fds, nfds, timeout)
#define	thr_pread(d, buf, nbytes, offset)
#define	thr_pwrite(d, buf, nbytes, offset)
#define	thr_read(d, buf, nbytes)
#define	thr_readv(d, iov, iovcnt)
#define	thr_select(nfds, readfds, writefds, exceptfds, timeout)
#define thr_setitimer(which, itv, oitv)
#define	thr_sigaction(sig, act, oact)
#define	thr_sigmask(how, set, oset)
#define	thr_sigsuspend(sigmask)
#define	thr_timedwait(set, signo, timeout)
#define	thr_wait4(wpid, status, options, rusage)
#define	thr_write(d, buf, nbytes)
#define	thr_writev(d, iov, iovcnt)

#define	FLOCKFILE(fp)
#define	FUNLOCKFILE(fp)

#endif /* _REENTRANT */


