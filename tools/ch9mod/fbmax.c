// SPDX-License-Identifier: GPL-2.0
/*
 * fbmax — ask EP 0x82 for more than 3 bytes, which userspace cannot do.
 *
 * #211: the playback feedback endpoint completes with -EOVERFLOW on every
 * packet and delivers zero bytes. EOVERFLOW on an isochronous IN is babble --
 * the device put more on the wire than the host scheduled for -- and the host
 * schedules 3 because wMaxPacketSize says 3. So schedule more and see what
 * comes back.
 *
 * WHY THIS CANNOT BE DONE FROM USERSPACE. tools/fbprobe.py tried, through raw
 * usbfs. usbcore validates every iso_frame_desc[].length against the endpoint's
 * declared wMaxPacketSize in proc_do_submiturb() and refuses with EMSGSIZE
 * before the URB reaches the host controller. That is not a permissions check
 * that root can step around -- the kernel is enforcing the DEVICE's own
 * declaration. The only way past it is to change what the host believes the
 * endpoint declared, which means writing to the cached descriptor, which means
 * being in the kernel.
 *
 * WHAT IT DECIDES.
 *
 *   packets come back with actual_length 4 or 8, status 0
 *       -> the device is emitting a whole buffer, not IEPDCNTX2's 3 bytes.
 *          EP_BSIZE() cannot express a buffer smaller than 8, so the endpoint
 *          is structurally unable to send 3. Confirms the FINDING_211
 *          hypothesis, and the bytes show whether the 10.14 value inside is
 *          even correct.
 *   packets still overflow at 8, or come back 0-length with status 0
 *       -> the hypothesis is wrong and the size is not the story.
 *
 * Either way this is worth a flash's worth of information without spending one.
 *
 * NO CONCURRENT STREAM. The module selects the altsetting itself, so nothing
 * else is submitting to EP 0x82 while the test runs:
 *
 *     sudo insmod fbmax.ko busnum=2 devnum=6 pktsize=8
 *     dmesg | tail -20
 *
 * That is a correction, not a convenience. The first version required a running
 * aplay so ALSA would select alt 1, which made snd-usb-audio a second submitter
 * on the same isochronous stream -- the leading explanation for the -EINVAL in
 * #213. It also meant the raised wMaxPacketSize would have sized ALSA's URBs
 * too. Owning the altsetting removes both problems.
 *
 * State touched, all restored: the altsetting (back to 0 on the way out) and
 * the cached wMaxPacketSize (restored in the same function that raises it). No
 * device state changes and no replug is involved.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/usb.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("#211: schedule oversized iso IN packets on the Mbox feedback EP");

static int busnum;
static int devnum;
static int epaddr = 0x82;
static int pktsize = 8;
static int npkts = 16;
module_param(busnum, int, 0444);
module_param(devnum, int, 0444);
module_param(epaddr, int, 0444);
MODULE_PARM_DESC(epaddr, "endpoint address (default 0x82, the feedback EP)");
module_param(pktsize, int, 0444);
MODULE_PARM_DESC(pktsize, "bytes to schedule per packet (3 = the control arm)");
module_param(npkts, int, 0444);

struct find_ctx {
	int busnum, devnum;
	struct usb_device *found;
};

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

/*
 * Locate which interface and alternate setting DECLARE the endpoint, across
 * every altsetting rather than only the active one.
 *
 * The first version searched only cur_altsetting and required the operator to
 * start a playback stream so ALSA would select alt 1. That made snd-usb-audio a
 * concurrent submitter on the same isochronous stream, which is the leading
 * explanation for the -EINVAL in #213. Owning the altsetting ourselves means
 * nothing else is submitting to EP 0x82 while the test runs.
 */
static int find_ep_altsetting(struct usb_device *udev, int addr,
			      int *ifnum, int *altnum)
{
	struct usb_host_config *cfg = udev->actconfig;
	int i, j, k;

	if (!cfg)
		return -ENODEV;
	for (i = 0; i < cfg->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = cfg->interface[i];

		if (!intf)
			continue;
		for (j = 0; j < intf->num_altsetting; j++) {
			struct usb_host_interface *alt = &intf->altsetting[j];

			for (k = 0; k < alt->desc.bNumEndpoints; k++) {
				if (alt->endpoint[k].desc.bEndpointAddress == addr) {
					*ifnum = alt->desc.bInterfaceNumber;
					*altnum = alt->desc.bAlternateSetting;
					return 0;
				}
			}
		}
	}
	return -ENODEV;
}

static void fb_complete(struct urb *urb)
{
	complete(urb->context);
}

static int __init fbmax_init(void)
{
	struct find_ctx c = { .busnum = busnum, .devnum = devnum };
	struct usb_device *udev;
	struct usb_host_endpoint *ep;
	struct urb *urb = NULL;
	DECLARE_COMPLETION_ONSTACK(done);
	unsigned char *buf = NULL;
	__le16 saved_wmax;
	int ret, i, nonzero = 0;
	int ifnum = -1, altnum = -1, restore_alt = 0;

	if (!busnum || !devnum) {
		pr_err("fbmax: need busnum= and devnum=\n");
		return -EINVAL;
	}
	if (npkts < 1 || npkts > 128 || pktsize < 1 || pktsize > 1023) {
		pr_err("fbmax: implausible npkts/pktsize\n");
		return -EINVAL;
	}

	usb_for_each_dev(&c, match_dev);
	udev = c.found;
	if (!udev) {
		pr_err("fbmax: no device at bus %d dev %d\n", busnum, devnum);
		return -ENODEV;
	}

	ret = find_ep_altsetting(udev, epaddr, &ifnum, &altnum);
	if (ret) {
		pr_err("fbmax: EP 0x%02x is not declared in any altsetting\n", epaddr);
		goto out_dev;
	}
	pr_info("fbmax: EP 0x%02x is on interface %d alt %d\n",
		epaddr, ifnum, altnum);

	/* Activate it ourselves. No aplay, so no second submitter. */
	ret = usb_set_interface(udev, ifnum, altnum);
	if (ret) {
		pr_err("fbmax: usb_set_interface(%d, %d) -> %d\n",
		       ifnum, altnum, ret);
		goto out_dev;
	}
	restore_alt = 1;

	ep = udev->ep_in[usb_pipeendpoint(usb_rcvisocpipe(udev, epaddr & 0x0F))];
	if (!ep) {
		pr_err("fbmax: udev->ep_in[%d] is still NULL after set_interface\n",
		       epaddr & 0x0F);
		ret = -ENODEV;
		goto out_alt;
	}

	/* ep is taken straight from udev->ep_in[], which is what
	 * usb_submit_urb() resolves the pipe to -- so the wMaxPacketSize patch
	 * below is guaranteed to be the one the submit path reads. An earlier
	 * version patched the descriptor found by walking the config and checked
	 * afterwards that the two matched; taking it from ep_in[] removes the
	 * question rather than testing for it. */
	saved_wmax = ep->desc.wMaxPacketSize;
	pr_info("fbmax: EP 0x%02x declares wMaxPacketSize %d; scheduling %d x %dB\n",
		epaddr, usb_endpoint_maxp(&ep->desc), npkts, pktsize);

	buf = kzalloc(pktsize * npkts, GFP_KERNEL);
	urb = usb_alloc_urb(npkts, GFP_KERNEL);
	if (!buf || !urb) {
		ret = -ENOMEM;
		goto out_free;
	}

	/* The whole point: make the host believe the endpoint is bigger. */
	ep->desc.wMaxPacketSize = cpu_to_le16(pktsize);

	urb->dev = udev;
	urb->pipe = usb_rcvisocpipe(udev, usb_endpoint_num(&ep->desc));
	/* URB_DIR_IN is NOT optional and NOT derived from the pipe. The
	 * usb_fill_*_urb() helpers set it for you; this URB is built by hand
	 * because none of those helpers cover isochronous, and omitting it made
	 * usb_submit_urb() return -EINVAL on every attempt -- including the
	 * 3-byte control arm, which is how the mistake was caught as mine rather
	 * than read as a device result. */
	urb->transfer_flags = URB_ISO_ASAP | URB_DIR_IN;
	urb->transfer_buffer = buf;
	urb->transfer_buffer_length = pktsize * npkts;
	urb->number_of_packets = npkts;
	urb->interval = 1;
	urb->context = &done;
	urb->complete = fb_complete;
	for (i = 0; i < npkts; i++) {
		urb->iso_frame_desc[i].offset = i * pktsize;
		urb->iso_frame_desc[i].length = pktsize;
	}

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		/* Print the things usb_submit_urb() rejects on, so a refusal names
		 * itself instead of needing the kernel source read backwards. */
		pr_err("fbmax: usb_submit_urb -> %d  (speed=%d xfertype=%d ep=%d "
		       "interval=%d npkts=%d maxp=%d flags=0x%x)\n",
		       ret, udev->speed, usb_endpoint_type(&ep->desc),
		       usb_endpoint_num(&ep->desc), urb->interval, npkts,
		       usb_endpoint_maxp(&ep->desc), urb->transfer_flags);
		ep->desc.wMaxPacketSize = saved_wmax;
		goto out_free;
	}

	if (!wait_for_completion_timeout(&done, msecs_to_jiffies(2000))) {
		pr_err("fbmax: URB did not complete; killing it\n");
		usb_kill_urb(urb);
	}

	/* Restore BEFORE reporting, so the window stays one URB wide even if the
	 * printks below are slow. */
	ep->desc.wMaxPacketSize = saved_wmax;

	pr_info("fbmax: urb status %d, error_count %d\n",
		urb->status, urb->error_count);
	for (i = 0; i < npkts; i++) {
		struct usb_iso_packet_descriptor *d = &urb->iso_frame_desc[i];

		if (d->actual_length) {
			nonzero++;
			pr_info("fbmax:  pkt %2d: %d bytes, status %d, data %*ph\n",
				i, d->actual_length, d->status,
				min(d->actual_length, 16u), buf + d->offset);
		} else {
			pr_info("fbmax:  pkt %2d: 0 bytes, status %d\n",
				i, d->status);
		}
	}
	pr_info("fbmax: VERDICT -- %d of %d packets carried data at a %dB schedule\n",
		nonzero, npkts, pktsize);
	if (nonzero)
		pr_info("fbmax: the device DOES emit more than 3 bytes; see the data "
			"above for the 10.14 value it is actually sending\n");
	else
		pr_info("fbmax: still nothing at %dB -- packet size is not the whole "
			"story (FINDING_211 hypothesis weakened)\n", pktsize);

	ret = -EAGAIN;		/* never stay resident */

out_free:
	usb_free_urb(urb);
	kfree(buf);
out_alt:
	/* Put the interface back to alt 0. snd-usb-audio selects its own
	 * altsetting when a stream opens, so leaving alt 1 selected would not
	 * break it -- but leaving the bus in a state this module chose, after
	 * this module has unloaded, is how the next measurement gets a surprise
	 * it cannot attribute. */
	if (restore_alt) {
		int r = usb_set_interface(udev, ifnum, 0);

		if (r)
			pr_warn("fbmax: could not restore interface %d to alt 0: %d\n",
				ifnum, r);
	}
out_dev:
	usb_put_dev(udev);
	return ret ? ret : -EAGAIN;
}

static void __exit fbmax_exit(void)
{
}

module_init(fbmax_init);
module_exit(fbmax_exit);
