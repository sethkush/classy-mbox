# Out-of-tree build for ch9addr. See ch9addr.c for what it does and why it is
# safe by default.
#
# The module must be built against headers matching the RUNNING kernel or it
# will not load. On the void box that means `uname -r` and the installed
# linux6.18-headers must agree; if they do not, install the matching kernel and
# reboot rather than forcing vermagic. Forcing it on a box 1 km away risks a
# struct-layout mismatch, which is memory corruption rather than a clean error.
KDIR ?= /lib/modules/$(shell uname -r)/build
obj-m := ch9addr.o

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
