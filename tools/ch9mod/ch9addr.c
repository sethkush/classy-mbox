// SPDX-License-Identifier: GPL-2.0
/*
 * ch9addr — the one Chapter 9 subject that needs kernel context.
 *
 * WHY A MODULE AT ALL. tools/ch9_probe.py covers every §9.4 request that libusb
 * can issue, and tools/usbtrace.bt cross-checks the results from the wire.
 * SET_ADDRESS is the exception: usbfs will not pass it through, because the
 * host stack owns the address map and a userspace program that moved the device
 * would leave usbcore addressing a device that is no longer there. USB20CV can
 * test it because it drives enumeration itself. This is how we get the same
 * reach without Windows.
 *
 * WHAT IS ALREADY PROVEN WITHOUT THIS. The SET_ADDRESS happy path is exercised
 * on every single enumeration -- the device would not appear at all if it did
 * not accept an address in the Default state -- and address retention across
 * SET_CONFIGURATION is proven every time ch9_probe runs a request after
 * SET_CONFIGURATION(1). So this module is NOT for the common case. It exists
 * for the edge cases nothing else reaches.
 *
 * THE SAFETY ARGUMENT, which is the reason this is shaped the way it is.
 *
 * Test 1 (default) sends SET_ADDRESS with a value above 127. USB 2.0 §9.4.6
 * makes that a Request Error, so a correct device STALLs and ITS ADDRESS DOES
 * NOT CHANGE -- the test is a no-op on a passing device. The only way to lose
 * the unit is for the device to WRONGLY ACCEPT it, which is exactly the defect
 * being looked for, and which is recoverable: a port reset returns any device
 * to the Default state and usbcore re-enumerates it. `uhubctl -a cycle` is a
 * genuine bus reset on both hosts here -- that is settled, and it is precisely
 * the recovery this needs. No physical access required.
 *
 * Test 2 (SET_ADDRESS(0), returning the device to the Default state) is behind
 * allow_risky=1 and is NOT recommended remotely. It deliberately un-addresses a
 * working device and relies on re-enumeration to bring it back.
 *
 * Every unit on this bench is 1 km from the person who can replug it, so the
 * default configuration cannot strand a conforming device, and the failure mode
 * of a non-conforming one is a bus reset away.
 *
 *   make -C tools/ch9mod
 *   sudo insmod ch9addr.ko busnum=2 devnum=6
 *   dmesg | tail
 *   sudo rmmod ch9addr
 *
 * The module does its work in init and reports through dmesg; it holds no state
 * and unloading it is unconditional.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/usb.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Chapter 9 SET_ADDRESS edge cases for the Mbox (#192)");

static int busnum;
static int devnum;
static int allow_risky;
module_param(busnum, int, 0444);
MODULE_PARM_DESC(busnum, "USB bus number, from lsusb");
module_param(devnum, int, 0444);
MODULE_PARM_DESC(devnum, "USB device number, from lsusb");
module_param(allow_risky, int, 0444);
MODULE_PARM_DESC(allow_risky,
		 "also run SET_ADDRESS(0), which un-addresses a working device");

struct find_ctx {
	int busnum;
	int devnum;
	struct usb_device *found;
};

static int match_dev(struct usb_device *udev, void *data)
{
	struct find_ctx *c = data;

	if (udev->bus && udev->bus->busnum == c->busnum &&
	    udev->devnum == c->devnum) {
		c->found = usb_get_dev(udev);
		return 1;			/* stop the walk */
	}
	return 0;
}

/*
 * Returns the raw usb_control_msg() result. -EPIPE is the device STALLing,
 * which for every test here is the PASS. Anything >= 0 means the device
 * accepted the request.
 */
static int set_address(struct usb_device *udev, int addr)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			       USB_REQ_SET_ADDRESS,
			       USB_DIR_OUT | USB_TYPE_STANDARD |
				       USB_RECIP_DEVICE,
			       addr, 0, NULL, 0, 5000);
}

static void report(const char *what, int addr, int ret, bool stall_is_pass)
{
	bool stalled = (ret == -EPIPE);
	bool pass = stall_is_pass ? stalled : (ret >= 0);

	pr_info("ch9addr: %s SET_ADDRESS(%d) -> %d %s  [%s]\n",
		pass ? "PASS" : "FAIL", addr, ret,
		stalled ? "(STALL)" : (ret >= 0 ? "(accepted)" : "(error)"),
		what);

	if (!pass && !stall_is_pass)
		pr_warn("ch9addr: the device refused a request it must accept\n");
	if (!pass && stall_is_pass && ret >= 0)
		pr_warn("ch9addr: DEVICE ACCEPTED AN ILLEGAL ADDRESS. Its address "
			"may have changed and usbcore does not know. Recover "
			"with a port reset: uhubctl -a cycle -l <loc>\n");
}

static int __init ch9addr_init(void)
{
	struct find_ctx c = { .busnum = busnum, .devnum = devnum };
	struct usb_device *udev;
	int ret;

	if (!busnum || !devnum) {
		pr_err("ch9addr: need busnum= and devnum= (see lsusb)\n");
		return -EINVAL;
	}

	usb_for_each_dev(&c, match_dev);
	udev = c.found;
	if (!udev) {
		pr_err("ch9addr: no device at bus %d dev %d\n", busnum, devnum);
		return -ENODEV;
	}

	pr_info("ch9addr: target %04x:%04x at bus %d dev %d, current address %d\n",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		busnum, devnum, udev->devnum);

	/*
	 * §9.4.6: "If the specified device address is greater than 127 ... the
	 * device shall respond with a Request Error." 128 and 200 are both
	 * illegal; two values rather than one because a device that masks the
	 * address to 7 bits would pass 128 (which masks to 0) for the wrong
	 * reason while still failing 200.
	 */
	ret = set_address(udev, 200);
	report("address > 127 must be a Request Error", 200, ret, true);

	/*
	 * STOP if the device accepted. The first version of this ran both values
	 * unconditionally and reported 200 FAIL / 128 PASS -- which read as "it
	 * gets the boundary right and only fails on large values", and was an
	 * artefact. By the time 128 was sent the device had already taken the
	 * illegal address and was no longer answering where the host was asking,
	 * so that second result describes a device that was not listening. A test
	 * that runs after the thing under test has changed state is not a test.
	 */
	if (ret >= 0) {
		pr_warn("ch9addr: skipping the remaining address tests -- the device "
			"moved, so any further result would describe a device that is "
			"no longer answering here\n");
		goto done;
	}

	ret = set_address(udev, 128);
	report("address > 127, boundary value", 128, ret, true);
	if (ret >= 0)
		goto done;

	if (allow_risky) {
		/*
		 * §9.4.6: SET_ADDRESS(0) returns the device to the Default state.
		 * Issuing it is not the test -- an ACK only proves the device
		 * answered, not that it acted. The test is that it STOPS answering
		 * at the address the host is still using, because that is what
		 * "returned to Default state" means from out here.
		 *
		 * So the pass condition below is a FAILED transfer, which is the
		 * one case where an error is the result rather than a problem.
		 */
		/* usb_control_msg() DMA-maps the buffer, so it must be kmalloc'd.
		 * The first version used `unsigned char probe[18]` on the stack and
		 * tripped WARNING at drivers/usb/core/hcd.c:1487 in
		 * usb_hcd_map_urb_for_dma. It also returned -EAGAIN, which read
		 * exactly like "the device stopped answering" -- i.e. it looked like
		 * the PASS this test is looking for. A broken instrument that fails
		 * in the shape of the expected result is the worst kind, so this is
		 * a heap buffer and the warning is gone. */
		unsigned char *probe = kmalloc(18, GFP_KERNEL);

		if (!probe) {
			ret = -ENOMEM;
			goto done;
		}

		pr_warn("ch9addr: allow_risky=1, un-addressing the device\n");
		ret = set_address(udev, 0);
		report("SET_ADDRESS(0) accepted", 0, ret, false);

		ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				      USB_REQ_GET_DESCRIPTOR,
				      USB_DIR_IN | USB_TYPE_STANDARD |
					      USB_RECIP_DEVICE,
				      (USB_DT_DEVICE << 8), 0,
				      probe, 18, 2000);
		kfree(probe);
		if (ret < 0)
			pr_info("ch9addr: PASS device no longer answers at its old "
				"address (%d) -- it really did return to Default "
				"state\n", ret);
		else
			pr_warn("ch9addr: FAIL device still answered %d bytes at its "
				"OLD address after SET_ADDRESS(0) -- it ACKed the "
				"request and did not act on it\n", ret);

		/* ---- DEFAULT-STATE TESTS -------------------------------------
		 *
		 * This was written off as "only USB20CV can reach this, because it
		 * drives enumeration". That was wrong, and the route was already
		 * built: SET_ADDRESS(0) above puts the device in the Default state,
		 * which means it is now answering AT ADDRESS 0. usbcore addresses
		 * transfers from udev->devnum, so pointing that at 0 talks to it
		 * there. No enumeration control needed -- just the state.
		 *
		 * Restoring is a plain SET_ADDRESS back to the original value, so
		 * the happy path costs nothing. A port cycle is still the fallback.
		 */
		{
			int saved = udev->devnum;
			unsigned char *b = kmalloc(18, GFP_KERNEL);

			udev->devnum = 0;
			pr_info("ch9addr: --- Default state (talking to address 0) ---\n");

			if (b) {
				/* §9.6.1: the 8-byte read every host makes first, before
				 * it knows bMaxPacketSize0. If this fails in Default
				 * state, nothing can ever enumerate the device. */
				ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
						      USB_REQ_GET_DESCRIPTOR,
						      USB_DIR_IN | USB_TYPE_STANDARD |
							      USB_RECIP_DEVICE,
						      (USB_DT_DEVICE << 8), 0, b, 8, 2000);
				pr_info("ch9addr: %s GET_DESCRIPTOR(device,8) @addr0 -> %d%s\n",
					(ret == 8) ? "PASS" : "FAIL", ret,
					(ret == 8) ? "" : "  [required by §9.6.1]");
				if (ret == 8)
					pr_info("ch9addr:      bLength=%u bDescriptorType=%u "
						"bMaxPacketSize0=%u\n", b[0], b[1], b[7]);

				/* SET_CONFIGURATION in the Default state.
				 *
				 * NOT scored, deliberately. The first version called
				 * accepting it a FAIL on the strength of "§9.4.7: not
				 * valid before the device is addressed" -- which I wrote
				 * without checking, and which is stronger than what the
				 * spec says. USB 2.0 §9.4 marks several requests' Default
				 * state behaviour as NOT SPECIFIED rather than requiring a
				 * Request Error, and if this is one of them then accepting
				 * it is legal and a FAIL here is a fabricated defect.
				 *
				 * So it is reported as an observation with the spec
				 * question named. Someone with the document open can score
				 * it; until then this records what the device does and
				 * does not pretend to know whether that is wrong. The
				 * §5.6.3-vs-§5.6.4 mess earlier the same day is why. */
				ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
						      USB_REQ_SET_CONFIGURATION,
						      USB_DIR_OUT | USB_TYPE_STANDARD |
							      USB_RECIP_DEVICE,
						      1, 0, NULL, 0, 2000);
				pr_info("ch9addr: OBSERVED SET_CONFIGURATION(1) @addr0 -> %d "
					"(%s). NOT SCORED -- §9.4 may mark Default-state "
					"behaviour for this request as 'not specified', in which "
					"case either answer is legal. Verify against the document "
					"before calling it either way.\n",
					ret, (ret < 0) ? "refused" : "accepted");
				kfree(b);
			}

			/* Put it back where usbcore thinks it is. */
			ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
					      USB_REQ_SET_ADDRESS,
					      USB_DIR_OUT | USB_TYPE_STANDARD |
						      USB_RECIP_DEVICE,
					      saved, 0, NULL, 0, 5000);
			udev->devnum = saved;

			/* VERIFY THE RESTORE. The first version printed "re-addressed
			 * to %d; NO port cycle needed" as soon as SET_ADDRESS returned
			 * >= 0 -- an ACK, not evidence. The unit was in fact NOT back:
			 * the next run found every request stalling, and it needed a
			 * port cycle after all.
			 *
			 * That is the same mistake this file warns about forty lines
			 * up, about SET_ADDRESS(0): "an ACK only proves the device
			 * answered, not that it acted". Written, and then not applied
			 * to the restore path in the same function. A claim that the
			 * device is healthy has to be a transfer that worked. */
			if (ret >= 0) {
				unsigned char *v = kmalloc(18, GFP_KERNEL);
				int chk = -ENOMEM;

				if (v) {
					msleep(20);   /* §9.2.6.3 recovery interval, generously */
					chk = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
							      USB_REQ_GET_DESCRIPTOR,
							      USB_DIR_IN | USB_TYPE_STANDARD |
								      USB_RECIP_DEVICE,
							      (USB_DT_DEVICE << 8), 0,
							      v, 18, 2000);
					kfree(v);
				}
				if (chk == 18) {
					pr_info("ch9addr: re-addressed to %d and VERIFIED "
						"answering; no port cycle needed\n", saved);
					goto done;
				}
				pr_warn("ch9addr: SET_ADDRESS(%d) was ACKed but the device "
					"does NOT answer there (%d)\n", saved, chk);
			} else {
				pr_warn("ch9addr: could not re-address to %d (%d)\n",
					saved, ret);
			}
			pr_warn("ch9addr: THE UNIT IS DOWN. Recover with:\n");
			pr_warn("ch9addr:     uhubctl -l <hub> -p <port> -a cycle\n");
		}
	} else {
		pr_info("ch9addr: SET_ADDRESS(0) skipped (allow_risky=0)\n");
	}

done:
	usb_put_dev(udev);

	/*
	 * Refuse to stay resident. Everything above ran in init, there is no
	 * ongoing work, and a module that lingers is a module someone has to
	 * remember to unload before the next kernel upgrade.
	 */
	return -EAGAIN;
}

static void __exit ch9addr_exit(void)
{
}

module_init(ch9addr_init);
module_exit(ch9addr_exit);
