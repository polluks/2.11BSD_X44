# $FreeBSD$

.include <bsd.init.mk>
.include "${.CURDIR}/../../Makefile.inc" 

PROG=	btx
INTERNALPROG=
SRCS=	btx.S

.if defined(BOOT_BTX_NOHANG)
BOOT_BTX_FLAGS=0x1
.else
BOOT_BTX_FLAGS=0x0
.endif

CFLAGS+=-DBTX_FLAGS=${BOOT_BTX_FLAGS}
CFLAGS+=-I${.CURDIR}/../../common

.if defined(BTX_SERIAL)
BOOT_COMCONSOLE_PORT?= 0x3f8
BOOT_COMCONSOLE_SPEED?= 9600
B2SIOFMT?=	0x3

CFLAGS+=-DBTX_SERIAL -DSIOPRT=${BOOT_COMCONSOLE_PORT} \
	-DSIOFMT=${B2SIOFMT} -DSIOSPD=${BOOT_COMCONSOLE_SPEED}
.endif

ORG=	0x9000

LDFLAGS+=${LDFLAGS_BIN}

.include <bsd.prog.mk>
