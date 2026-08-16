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

	ret = set_address(udev, 128);
	report("address > 127, boundary value", 128, ret, true);

	if (allow_risky) {
		pr_warn("ch9addr: allow_risky=1, un-addressing the device\n");
		ret = set_address(udev, 0);
		report("SET_ADDRESS(0) returns to Default state", 0, ret, false);
		pr_warn("ch9addr: the device is now unaddressed; a port reset is "
			"required to bring it back\n");
	} else {
		pr_info("ch9addr: SET_ADDRESS(0) skipped (allow_risky=0)\n");
	}

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
