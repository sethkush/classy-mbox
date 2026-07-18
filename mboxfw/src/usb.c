/*
 * USB glue — stub.
 *
 * TODO: full UAC1 descriptor set + endpoint 0 request handling +
 * endpoint 3 audio streaming. See TI's Application/devDesc.c and
 * ROM/Usbaudio.c for the reference implementation we can port.
 */

#include "regs.h"

void usb_init(void)
{
    /* Descriptor upload + endpoint 0 arm goes here. */
}

void usb_service(void)
{
    /* EP0 setup handling + audio EP3 buffer swap goes here. */
}
