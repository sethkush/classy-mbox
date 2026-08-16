// SPDX-License-Identifier: GPL-2.0
/*
 * eptoggle — does the device reset its data toggle when a halt is cleared?
 *
 * USB 2.0 §5.8.5 requires an endpoint's data toggle to reset to DATA0 when the
 * host clears its halt, and §9.4.5's CLEAR_FEATURE(ENDPOINT_HALT) is how that
 * happens. A device that clears the halt but keeps its toggle is subtly and
 * badly broken: the host resets ITS toggle, so every packet the device sends
 * afterwards arrives with the wrong PID and is discarded as a retransmission.
 * The endpoint looks alive and delivers nothing, forever.
 *
 * NEITHER libusb NOR USB20CV CAN SEE THIS. Toggle bits live in the host
 * controller's queue heads, not on any interface either tool can read. A bus
 * analyser can watch the PIDs; short of that, the only way to observe it is
 * from inside the kernel, where usb_device->toggle[] holds the bits. That makes this a test the reference tool does
 * not have, rather than a substitute for it.
 *
 * WHY IT IS ONLY POSSIBLE NOW. The test needs an endpoint that can actually
 * halt. Until #214 this device stalled SET_FEATURE(ENDPOINT_HALT) on every
 * endpoint -- correctly for the three isochronous ones (§9.4.5 requires halt
 * of interrupt and bulk endpoints), and wrongly for EP 0x83, which is
 * interrupt. Fixing that made this measurable.
 *
 * WHAT IT DOES.
 *   1. Reads the host's toggle for EP 0x83 and transfers once, to establish
 *      that data flows at all. THIS IS THE REFERENCE ARM: if nothing arrives
 *      before the halt, nothing arriving after it proves nothing.
 *   2. SET_FEATURE(ENDPOINT_HALT), then GET_STATUS to confirm it took.
 *   3. CLEAR_FEATURE(ENDPOINT_HALT).
 *   4. Reads the host's toggle again -- usbcore resets it in usb_clear_halt(),
 *      so this checks the HOST side did what the spec expects.
 *   5. Transfers again. If the device kept its toggle while the host reset
 *      its own, this transfer times out or returns nothing while the endpoint
 *      reports perfect health. That asymmetry IS the defect.
 *
 * Safe: it halts and unhalts one interrupt endpoint that carries status only,
 * and nothing streams on it. If step 3 fails the endpoint stays halted until a
 * port cycle, which is a command rather than a trip.
 *
 *     sudo insmod eptoggle.ko busnum=2 devnum=23
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("#5.8.5 data-toggle reset check for the Mbox status endpoint");

static int busnum;
static int devnum;
static int epaddr = 0x83;
module_param(busnum, int, 0444);
module_param(devnum, int, 0444);
module_param(epaddr, int, 0444);
MODULE_PARM_DESC(epaddr, "interrupt endpoint to exercise (default 0x83)");

/* usb_gettoggle()/usb_settoggle() are no longer in linux/usb.h on 6.18 -- the
 * inline accessors went away, but the state they wrapped did not. struct
 * usb_device still carries `unsigned int toggle[2]`, documented in that header
 * as "one bit for each endpoint, with ([0] = IN, [1] = OUT)". So the bit is read
 * directly rather than through a helper that no longer exists. */
static inline int host_toggle_in(struct usb_device *udev, int epnum)
{
	return (udev->toggle[0] >> epnum) & 1;
}

struct find_ctx { int busnum, devnum; struct usb_device *found; };

static int match_dev(struct usb_device *udev, void *data)
{
	struct find_ctx *c = data;

	if (udev->bus && udev->bus->busnum == c->busnum &&
	    udev->devnum == c->devnum) {
		c->found = usb_get_dev(udev);
		return 1;
	}
	return 0;
}

/* -> bytes received, or the negative errno. */
static int poll_ep(struct usb_device *udev, int ep, int len, int timeout)
{
	unsigned char *buf = kmalloc(len, GFP_KERNEL);
	int actual = 0, ret;

	if (!buf)
		return -ENOMEM;
	/* kmalloc, not stack: usb_interrupt_msg DMA-maps the buffer, and a stack
	 * buffer trips WARNING at hcd.c:1487 and returns an error that reads
	 * exactly like the device being silent. That mistake was made in
	 * ch9addr on 2026-08-16 and reported a clean PASS. */
	ret = usb_interrupt_msg(udev, usb_rcvintpipe(udev, ep & 0x0F),
				buf, len, &actual, timeout);
	kfree(buf);
	return ret ? ret : actual;
}

static int feat(struct usb_device *udev, int req, int ep)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), req,
			       USB_DIR_OUT | USB_TYPE_STANDARD |
				       USB_RECIP_ENDPOINT,
			       0 /* ENDPOINT_HALT */, ep, NULL, 0, 5000);
}

static int __init eptoggle_init(void)
{
	struct find_ctx c = { .busnum = busnum, .devnum = devnum };
	struct usb_device *udev;
	int epnum, t0, t1, before, after, ret;
	unsigned char *st;

	if (!busnum || !devnum) {
		pr_err("eptoggle: need busnum= and devnum=\n");
		return -EINVAL;
	}
	usb_for_each_dev(&c, match_dev);
	udev = c.found;
	if (!udev) {
		pr_err("eptoggle: no device at bus %d dev %d\n", busnum, devnum);
		return -ENODEV;
	}
	epnum = epaddr & 0x0F;

	/* --- reference arm ------------------------------------------------ */
	t0 = host_toggle_in(udev, epnum);
	before = poll_ep(udev, epaddr, 2, 1500);
	pr_info("eptoggle: BEFORE halt: host toggle=%d, transfer -> %d\n",
		t0, before);
	/* ETIMEDOUT COUNTS AS FAILURE HERE, and letting it through was a real
	 * mistake in the first version: the arm existed to prove data flows, and
	 * it accepted "nothing arrived" as good enough. The run then reported a
	 * confident PASS on an endpoint that never transferred at all, before or
	 * after -- the two states the test is supposed to distinguish. */
	if (before < 0) {
		pr_warn("eptoggle: VOID -- the endpoint did not transfer before the "
			"halt (%d), so nothing observed after it can be attributed to "
			"the toggle.\n", before);
		if (before == -ETIMEDOUT)
			pr_warn("eptoggle: EP 0x%02x is EVENT-DRIVEN -- it reports panel "
				"and status changes and NAKs when it has nothing to say, "
				"which is correct behaviour and makes it unusable for this "
				"test. §5.8.5 needs an endpoint that produces data on "
				"demand; this device has none that can also halt.\n",
				epaddr);
		goto out;
	}

	/* --- halt --------------------------------------------------------- */
	ret = feat(udev, USB_REQ_SET_FEATURE, epaddr);
	if (ret) {
		pr_warn("eptoggle: SET_FEATURE(HALT) -> %d; needs #214's fix in the "
			"running image\n", ret);
		goto out;
	}
	st = kmalloc(2, GFP_KERNEL);
	if (st) {
		ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				      USB_REQ_GET_STATUS,
				      USB_DIR_IN | USB_TYPE_STANDARD |
					      USB_RECIP_ENDPOINT,
				      0, epaddr, st, 2, 2000);
		pr_info("eptoggle: GET_STATUS after halt -> %d, halt bit=%d\n",
			ret, (ret >= 1) ? (st[0] & 1) : -1);
		kfree(st);
	}

	/* --- clear, then look at both sides -------------------------------- */
	ret = feat(udev, USB_REQ_CLEAR_FEATURE, epaddr);
	if (ret) {
		pr_err("eptoggle: CLEAR_FEATURE(HALT) -> %d -- the endpoint is now "
		       "STUCK HALTED until a port cycle\n", ret);
		goto out;
	}
	t1 = host_toggle_in(udev, epnum);
	after = poll_ep(udev, epaddr, 2, 1500);

	pr_info("eptoggle: AFTER clear: host toggle=%d, transfer -> %d\n",
		t1, after);

	if (t1 != 0)
		pr_warn("eptoggle: HOST-SIDE ODD -- usbcore left its toggle at %d; "
			"usb_clear_halt() should have reset it to 0\n", t1);

	if (after >= 0)
		pr_info("eptoggle: PASS -- data still flows after clear-halt, so the "
			"device reset its toggle in step with the host (§5.8.5)\n");
	else
		pr_warn("eptoggle: FAIL -- the endpoint reports healthy but no longer "
			"transfers (%d). That asymmetry is the signature of a device "
			"that cleared the halt and KEPT its data toggle: the host is "
			"on DATA0 and every packet it sends is discarded as a "
			"retransmission\n", after);

out:
	usb_put_dev(udev);
	return -EAGAIN;			/* never stay resident */
}

static void __exit eptoggle_exit(void) { }

module_init(eptoggle_init);
module_exit(eptoggle_exit);
