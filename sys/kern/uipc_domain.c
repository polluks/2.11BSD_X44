/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 *
 *	@(#)uipc_domain.c	7.2.1 (2.11BSD) 1995/10/09
 */

#include <sys/cdefs.h>

#include "opt_inet.h"
#include "opt_ipsec.h"
#include "opt_ns.h"
#include "arp.h"

#include <sys/param.h>
//#ifdef INET
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/mbuf.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/sysctl.h>

struct domain *domains;

void	pffasttimo(void *);
void	pfslowtimo(void *);

#define	ADDDOMAIN(x)  {                 \
    extern struct domain  x##domain;    \
    x##domain.dom_next = domains;       \
    domains = &x##domain;               \
}

void
domaininit(void)
{
	register struct domain *dp;
	register struct protosw *pr;

#ifndef lint
	ADDDOMAIN(unix);
#ifdef INET
	ADDDOMAIN(inet);
#endif
#ifdef INET6
	ADDDOMAIN(inet6);
#endif
#ifdef NS
	ADDDOMAIN(ns);
#endif
#if defined(IPSEC) || defined(FAST_IPSEC)
	ADDDOMAIN(key);
#endif
#ifdef INET
#if NARP > 0
	ADDDOMAIN(arp);
#endif
#endif
	ADDDOMAIN(route);
#endif /* ! lint */

	for (dp = domains; dp; dp = dp->dom_next) {
		if (dp->dom_init)
			(*dp->dom_init)();
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_init)
				(*pr->pr_init)();
	}

	if (max_linkhdr < 64) {
		max_linkhdr = 64;
	}

	max_hdr = max_linkhdr + max_protohdr;
	max_datalen = MHLEN - max_hdr;
	//null_init();

	timeout(pffasttimo, NULL, 1);
	timeout(pfslowtimo, NULL, 1);
}

struct domain *
pffinddomain(family)
	int family;
{
	register struct domain *dp;

	for (dp = domains; dp != NULL; dp = dp->dom_next) {
		if (dp->dom_family == family) {
			return (dp);
		}
	}
	return (NULL);
}

struct protosw *
pffindtype(family, type)
	int family, type;
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			goto found;
	return (0);
found:
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
		if (pr->pr_type && pr->pr_type == type)
			return (pr);
	return (0);
}

struct protosw *
pffindproto(family, protocol, type)
	int family, protocol, type;
{
	register struct domain *dp;
	register struct protosw *pr;
	struct protosw *maybe = 0;

	if (family == 0)
		return (0);
	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			goto found;
	return (0);
found:
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
		if ((pr->pr_protocol == protocol) && (pr->pr_type == type))
			return (pr);

		if (type == SOCK_RAW && pr->pr_type == SOCK_RAW &&
		    pr->pr_protocol == 0 && maybe == (struct protosw *)0)
			maybe = pr;
	}
	return (maybe);
}

int
net_sysctl(name, namelen, oldp, oldlenp, newp, newlen)
	int *name;
	u_int namelen;
	void *oldp;
	size_t *oldlenp;
	void *newp;
	size_t newlen;
{
	register struct domain *dp;
	register struct protosw *pr;
	int family, protocol;

	/*
	 * All sysctl names at this level are nonterminal;
	 * next two components are protocol family and protocol number,
	 * then at least one addition component.
	 */
	if (namelen < 3)
		return (EISDIR);		/* overloaded */
	family = name[0];
	protocol = name[1];

	if (family == 0)
		return (0);
	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			goto found;
	return (ENOPROTOOPT);
found:
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
		if (pr->pr_protocol == protocol && pr->pr_sysctl)
			return ((*pr->pr_sysctl)(name + 2, namelen - 2,
			    oldp, oldlenp, newp, newlen));
	return (ENOPROTOOPT);
}

void
pfctlinput(cmd, sa)
	int cmd;
	struct sockaddr *sa;
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_ctlinput)
				(*pr->pr_ctlinput)(cmd, sa, NULL);
}

void
pfctlinput2(cmd, sa, ctlparam)
	int cmd;
	struct sockaddr *sa;
	void *ctlparam;
{
	register struct domain *dp;
	register struct protosw *pr;

	if (sa == NULL) {
		return;
	}

	for (dp = domains; dp; dp = dp->dom_next) {
		if (dp->dom_family != sa->sa_family) {
			continue;
		}
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
			if (pr->pr_ctlinput) {
				(*pr->pr_ctlinput)(cmd, sa, ctlparam);
			}
		}
	}
}

void
pfslowtimo(arg)
	void *arg;
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_slowtimo)
				(*pr->pr_slowtimo)();
	timeout(pfslowtimo, NULL, hz / PR_SLOWHZ);
}

void
pffasttimo(arg)
	void *arg;
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_fasttimo)
				(*pr->pr_fasttimo)();
	timeout(pffasttimo, NULL, hz / PR_FASTHZ);
}
//#endif
