#	$NetBSD: Makefile,v 1.19 2021/04/12 00:05:56 mrg Exp $

UNSUPPORTED_COMPILER.clang=	# defined
LIBISCXX = yes

.include <bsd.own.mk>

.include "../Makefile.inc"
.include "../Makefile.sanitizer"

.PATH: ${SANITIZER}/ubsan

# XXXmknative
UBSAN_SRCS= \
	ubsan_diag.cc \
	ubsan_flags.cc \
	ubsan_handlers.cc \
	ubsan_init.cc \
	ubsan_monitor.cc \
	ubsan_type_hash.cc \
	ubsan_type_hash_itanium.cc \
	ubsan_type_hash_win.cc \
	ubsan_value.cc

.for _s in ubsan_handlers_cxx ubsan_type_hash ubsan_type_hash_itanium
COPTS.${_s}.cc+=-frtti
.endfor

.if ${MACHINE_ARCH} == "vax"
COPTS.sanitizer_file.cc += -O1
.endif

LIB=	ubsan
SRCS+=	${UBSAN_SRCS}
LIBDPLIBS+= m ${NETBSDSRCDIR}/usr.lib/libm
LIBDPLIBS+= rt ${NETBSDSRCDIR}/usr.lib/librt
LIBDPLIBS+= pthread ${NETBSDSRCDIR}/usr.lib/libpthread
CPPFLAGS+=-DCAN_SANITIZE_UB=1

# XXX
COPTS.ubsan_handlers.cc+=-Wno-error=maybe-uninitialized

.include <bsd.lib.mk>
