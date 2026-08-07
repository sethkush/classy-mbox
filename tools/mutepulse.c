/* mutepulse -- drive the capture gate low for a precise interval, then high.
 *
 * Exists because two `amixer` invocations cannot resolve the interval being
 * measured: each is a process spawn plus a control transfer, so the gap
 * between mute and unmute has a floor of tens of milliseconds and jitters. The
 * ladder in FINDING_197 could therefore only bound the required hold at
 * "<= 50 ms". One process holding the control handle open, with a single
 * usleep between two element writes, resolves to about a millisecond.
 *
 * Uses the ALSA control API rather than libusb SET_CUR so that snd-usb-audio
 * stays bound throughout -- the class Feature Unit request is INTERFACE
 * recipient and is refused with EBUSY while the driver holds the interface,
 * and detaching it would mean rebinding before every measurement.
 *
 *   mutepulse <card> <hold_ms>
 */
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char *ELEM = "PCM Capture Switch";

static int set_switch(snd_ctl_t *ctl, snd_ctl_elem_id_t *id, int on)
{
    snd_ctl_elem_value_t *val;
    snd_ctl_elem_info_t *info;
    unsigned int i, count;
    int err;

    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_info_set_id(info, id);
    if ((err = snd_ctl_elem_info(ctl, info)) < 0) return err;
    count = snd_ctl_elem_info_get_count(info);

    snd_ctl_elem_value_alloca(&val);
    snd_ctl_elem_value_set_id(val, id);
    for (i = 0; i < count; i++)
        snd_ctl_elem_value_set_boolean(val, i, on);
    return snd_ctl_elem_write(ctl, val);
}

int main(int argc, char **argv)
{
    char name[32];
    snd_ctl_t *ctl;
    snd_ctl_elem_id_t *id;
    int card = (argc > 1) ? atoi(argv[1]) : 0;
    double ms = (argc > 2) ? atof(argv[2]) : 50.0;
    int err;

    snprintf(name, sizeof(name), "hw:%d", card);
    if ((err = snd_ctl_open(&ctl, name, 0)) < 0) {
        fprintf(stderr, "open %s: %s\n", name, snd_strerror(err)); return 1;
    }

    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, ELEM);

    /* Both writes and the wait happen with the handle already open, so the
     * only cost inside the interval is one ioctl each side. */
    if ((err = set_switch(ctl, id, 0)) < 0) {
        fprintf(stderr, "mute: %s\n", snd_strerror(err)); return 1;
    }
    usleep((useconds_t)(ms * 1000.0));
    if ((err = set_switch(ctl, id, 1)) < 0) {
        fprintf(stderr, "unmute: %s\n", snd_strerror(err)); return 1;
    }

    snd_ctl_close(ctl);
    printf("card %d: gate held low %.2f ms, released\n", card, ms);
    return 0;
}
