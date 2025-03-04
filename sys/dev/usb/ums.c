/*	$NetBSD: ums.c,v 1.60 2003/03/11 16:44:00 augustss Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * HID spec: http://www.usb.org/developers/devclass_docs/HID1_11.pdf
 */

#include <sys/cdefs.h>
/* __KERNEL_RCSID(0, "$NetBSD: ums.c,v 1.60 2003/03/11 16:44:00 augustss Exp $"); */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/poll.h>
#include <sys/user.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usb_quirks.h>
#include <dev/usb/hid.h>

#include <dev/misc/wscons/wsconsio.h>
#include <dev/misc/wscons/wsmousevar.h>
#include <dev/usb/uhidev.h>

#ifdef USB_DEBUG
#define DPRINTF(x)		if (umsdebug) logprintf x
#define DPRINTFN(n,x)	if (umsdebug>(n)) logprintf x
int	umsdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UMS_BUT(i) ((i) == 1 || (i) == 2 ? 3 - (i) : i)

#define UMSUNIT(s)	(minor(s))

#define PS2LBUTMASK	x01
#define PS2RBUTMASK	x02
#define PS2MBUTMASK	x04
#define PS2BUTMASK 	0x0f

#define MAX_BUTTONS	7	/* must not exceed size of sc_buttons */

struct ums_softc {
	struct uhidev 		sc_hdev;

	struct hid_location sc_loc_x, sc_loc_y, sc_loc_z;
	struct hid_location sc_loc_btn[MAX_BUTTONS];

	int 				sc_enabled;

	int 				flags;		/* device configuration */
#define UMS_Z			0x01	/* z direction available */
#define UMS_SPUR_BUT_UP	0x02	/* spurious button up events */
#define UMS_REVZ		0x04	/* Z-axis is reversed */

	int 				nbuttons;

	u_int32_t 			sc_buttons;	/* mouse button status */
	struct device 		*sc_wsmousedev;

	char				sc_dying;
};

#define MOUSE_FLAGS_MASK 	(HIO_CONST|HIO_RELATIVE)
#define MOUSE_FLAGS 		(HIO_RELATIVE)

static void ums_intr(struct uhidev *addr, void *ibuf, u_int len);

static int	ums_enable(void *);
static void	ums_disable(void *);
static int	ums_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wsmouse_accessops ums_accessops = {
	ums_enable,
	ums_ioctl,
	ums_disable,
};

CFOPS_DECL(ums, ums_match, ums_attach, ums_detach, ums_activate);
CFDRIVER_DECL(NULL, ums, &ums_cops, DV_DULL, sizeof(struct ums_softc));

int
ums_match(struct device *parent, struct cfdata *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;
	int size;
	void *desc;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	if (!hid_is_collection(desc, size, uha->reportid,
			       HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE)))
		return (UMATCH_NONE);

	return (UMATCH_IFACECLASS);
}

void
ums_attach(struct device *parent, struct device *self, void *aux)
{
	struct ums_softc *sc = (struct ums_softc *)self;
	struct uhidev_attach_arg *uha = aux;
	struct wsmousedev_attach_args a;
	int size;
	void *desc;
	u_int32_t flags, quirks;
	int i, wheel;
	struct hid_location loc_btn;

	sc->sc_hdev.sc_intr = ums_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;

	quirks = usbd_get_quirks(uha->parent->sc_udev)->uq_flags;
	if (quirks & UQ_MS_REVZ)
		sc->flags |= UMS_REVZ;
	if (quirks & UQ_SPUR_BUT_UP)
		sc->flags |= UMS_SPUR_BUT_UP;

	uhidev_get_report_desc(uha->parent, &desc, &size);

	if (!hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X),
	       uha->reportid, hid_input, &sc->sc_loc_x, &flags)) {
		printf("\n%s: mouse has no X report\n",
		       sc->sc_hdev.sc_dev.dv_xname);
		return;
	}
	if ((flags & MOUSE_FLAGS_MASK) != MOUSE_FLAGS) {
		printf("\n%s: X report 0x%04x not supported\n",
		       sc->sc_hdev.sc_dev.dv_xname, flags);
		return;
	}

	if (!hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y),
	       uha->reportid, hid_input, &sc->sc_loc_y, &flags)) {
		printf("\n%s: mouse has no Y report\n",
		       sc->sc_hdev.sc_dev.dv_xname);
		return;
	}
	if ((flags & MOUSE_FLAGS_MASK) != MOUSE_FLAGS) {
		printf("\n%s: Y report 0x%04x not supported\n",
		       sc->sc_hdev.sc_dev.dv_xname, flags);
		return;
	}

	/* Try to guess the Z activator: first check Z, then WHEEL. */
	wheel = 0;
	if (hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Z),
	       uha->reportid, hid_input, &sc->sc_loc_z, &flags) ||
	    (wheel = hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP,
						       HUG_WHEEL),
	       uha->reportid, hid_input, &sc->sc_loc_z, &flags))) {
		if ((flags & MOUSE_FLAGS_MASK) != MOUSE_FLAGS) {
			sc->sc_loc_z.size = 0;	/* Bad Z coord, ignore it */
		} else {
			sc->flags |= UMS_Z;
			/* Wheels need the Z axis reversed. */
			if (wheel)
				sc->flags ^= UMS_REVZ;
		}
	}

	/* figure out the number of buttons */
	for (i = 1; i <= MAX_BUTTONS; i++)
		if (!hid_locate(desc, size, HID_USAGE2(HUP_BUTTON, i),
			uha->reportid, hid_input, &loc_btn, 0))
			break;
	sc->nbuttons = i - 1;

	printf(": %d button%s%s\n",
	    sc->nbuttons, sc->nbuttons == 1 ? "" : "s",
	    sc->flags & UMS_Z ? " and Z dir." : "");

	for (i = 1; i <= sc->nbuttons; i++)
		hid_locate(desc, size, HID_USAGE2(HUP_BUTTON, i),
			   uha->reportid, hid_input,
			   &sc->sc_loc_btn[i-1], 0);

#ifdef USB_DEBUG
	DPRINTF(("ums_attach: sc=%p\n", sc));
	DPRINTF(("ums_attach: X\t%d/%d\n",
		 sc->sc_loc_x.pos, sc->sc_loc_x.size));
	DPRINTF(("ums_attach: Y\t%d/%d\n",
		 sc->sc_loc_y.pos, sc->sc_loc_y.size));
	if (sc->flags & UMS_Z)
		DPRINTF(("ums_attach: Z\t%d/%d\n",
			 sc->sc_loc_z.pos, sc->sc_loc_z.size));
	for (i = 1; i <= sc->nbuttons; i++) {
		DPRINTF(("ums_attach: B%d\t%d/%d\n",
			 i, sc->sc_loc_btn[i-1].pos,sc->sc_loc_btn[i-1].size));
	}
#endif

	a.accessops = &ums_accessops;
	a.accesscookie = sc;

	sc->sc_wsmousedev = config_found(self, &a, wsmousedevprint);

	return;
}

int
ums_activate(struct device *self, enum devact act)
{
	struct ums_softc *sc = (struct ums_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_ACTIVATE:
		return (EOPNOTSUPP);

	case DVACT_DEACTIVATE:
		if (sc->sc_wsmousedev != NULL)
			rv = config_deactivate(sc->sc_wsmousedev);
		sc->sc_dying = 1;
		break;
	}
	return (rv);
}

int
ums_detach(struct device *self, int flags)
{
	struct ums_softc *sc = (struct ums_softc *)self;
	int rv = 0;

	DPRINTF(("ums_detach: sc=%p flags=%d\n", sc, flags));

	/* No need to do reference counting of ums, wsmouse has all the goo. */
	if (sc->sc_wsmousedev != NULL)
		rv = config_detach(sc->sc_wsmousedev, flags);

	return (rv);
}

void
ums_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	struct ums_softc *sc = (struct ums_softc *)addr;
	int dx, dy, dz;
	u_int32_t buttons = 0;
	int i;
	int s;

	DPRINTFN(5,("ums_intr: len=%d\n", len));

	dx =  hid_get_data(ibuf, &sc->sc_loc_x);
	dy = -hid_get_data(ibuf, &sc->sc_loc_y);
	dz =  hid_get_data(ibuf, &sc->sc_loc_z);
	if (sc->flags & UMS_REVZ)
		dz = -dz;
	for (i = 0; i < sc->nbuttons; i++)
		if (hid_get_data(ibuf, &sc->sc_loc_btn[i]))
			buttons |= (1 << UMS_BUT(i));

	if (dx != 0 || dy != 0 || dz != 0 || buttons != sc->sc_buttons) {
		DPRINTFN(10, ("ums_intr: x:%d y:%d z:%d buttons:0x%x\n",
			dx, dy, dz, buttons));
		sc->sc_buttons = buttons;
		if (sc->sc_wsmousedev != NULL) {
			s = spltty();
			wsmouse_input(sc->sc_wsmousedev, buttons, dx, dy, dz, WSMOUSE_INPUT_DELTA);
			splx(s);
		}
	}
}

static int
ums_enable(void *v)
{
	struct ums_softc *sc = v;

	DPRINTFN(1,("ums_enable: sc=%p\n", sc));

	if (sc->sc_dying)
		return (EIO);

	if (sc->sc_enabled)
		return (EBUSY);

	sc->sc_enabled = 1;
	sc->sc_buttons = 0;

	return (uhidev_open(&sc->sc_hdev));
}

static void
ums_disable(void *v)
{
	struct ums_softc *sc = v;

	DPRINTFN(1,("ums_disable: sc=%p\n", sc));
#ifdef DIAGNOSTIC
	if (!sc->sc_enabled) {
		printf("ums_disable: not enabled\n");
		return;
	}
#endif

	sc->sc_enabled = 0;
	uhidev_close(&sc->sc_hdev);
}

static int
ums_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)

{
	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_USB;
		return (0);
	}

	return (ENOIOCTL);
}
