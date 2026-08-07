/* rtlat.c -- exact round-trip latency on one card, via snd_pcm_link().
 *
 * Why this rather than alsabat: alsabat's --roundtriplatency starts at a
 * 48-frame buffer, which underruns instantly on a 1 ms full-speed USB device,
 * and its detector then reports "too much background noise". It never reaches
 * a workable buffer size on this hardware.
 *
 * The measurement problem is that the offset between when playback starts and
 * when capture starts is unknown, and it is exactly the size of the thing being
 * measured. snd_pcm_link() removes it: both substreams belong to the same card
 * and are started by a single trigger, so capture frame 0 and playback frame 0
 * are the same instant. Round-trip latency is then a subtraction of two frame
 * indices, not an inference.
 *
 * Playback is a Hann-windowed tone burst on the RIGHT channel only, because the
 * bench self-loop is A out2 -> A src2 (BENCH_WIRING.md). Left is left silent so
 * it serves as the control.
 *
 * Capture is written raw (S24_3LE, interleaved) to a file; the burst is located
 * by cross-correlation in Python, which gives sub-sample resolution and does
 * not care about the loop's ~-23 dB attenuation at minimum gain.
 */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CH 2

static int setup(snd_pcm_t *pcm, unsigned rate,
                 snd_pcm_uframes_t *period, snd_pcm_uframes_t *buffer)
{
    snd_pcm_hw_params_t *hw;
    snd_pcm_sw_params_t *sw;
    int err, dir = 0;

    snd_pcm_hw_params_alloca(&hw);
    if ((err = snd_pcm_hw_params_any(pcm, hw)) < 0) return err;
    if ((err = snd_pcm_hw_params_set_access(pcm, hw,
                    SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) return err;
    if ((err = snd_pcm_hw_params_set_format(pcm, hw,
                    SND_PCM_FORMAT_S24_3LE)) < 0) return err;
    if ((err = snd_pcm_hw_params_set_channels(pcm, hw, CH)) < 0) return err;
    if ((err = snd_pcm_hw_params_set_rate(pcm, hw, rate, 0)) < 0) return err;
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm, hw, period, &dir)) < 0)
        return err;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw, buffer)) < 0)
        return err;
    if ((err = snd_pcm_hw_params(pcm, hw)) < 0) return err;
    snd_pcm_hw_params_get_period_size(hw, period, &dir);
    snd_pcm_hw_params_get_buffer_size(hw, buffer);

    snd_pcm_sw_params_alloca(&sw);
    if ((err = snd_pcm_sw_params_current(pcm, sw)) < 0) return err;
    /* Never auto-start. The linked pair is started once, explicitly, so that
     * the trigger instant is a thing we chose rather than a side effect of a
     * buffer happening to fill. */
    if ((err = snd_pcm_sw_params_set_start_threshold(pcm, sw,
                    (snd_pcm_uframes_t)-1)) < 0) return err;
    if ((err = snd_pcm_sw_params_set_avail_min(pcm, sw, *period)) < 0) return err;
    if ((err = snd_pcm_sw_params(pcm, sw)) < 0) return err;
    return 0;
}

static void put24(unsigned char *p, double v)
{
    int s;
    if (v >  0.999999) v =  0.999999;
    if (v < -0.999999) v = -0.999999;
    s = (int)lrint(v * 8388607.0);
    p[0] = (unsigned char)( s        & 0xFF);
    p[1] = (unsigned char)((s >>  8) & 0xFF);
    p[2] = (unsigned char)((s >> 16) & 0xFF);
}

int main(int argc, char **argv)
{
    const char *dev     = (argc > 1) ? argv[1] : "hw:0,0";
    unsigned    rate    = (argc > 2) ? (unsigned)atoi(argv[2]) : 48000;
    snd_pcm_uframes_t period = (argc > 3) ? (snd_pcm_uframes_t)atoi(argv[3]) : 256;
    snd_pcm_uframes_t buffer = (argc > 4) ? (snd_pcm_uframes_t)atoi(argv[4]) : 2048;
    double      secs    = (argc > 5) ? atof(argv[5]) : 2.0;
    long        burstat = (argc > 6) ? atol(argv[6]) : 24000;
    const char *outpath = (argc > 7) ? argv[7] : "/tmp/rtlat_cap.raw";

    long   nframes = (long)(secs * rate);
    long   playframes;
    long   blen    = 512;                 /* burst length in frames */
    double bhz     = 2000.0;
    unsigned char *play, *cap;
    snd_pcm_t *ph = NULL, *ch_ = NULL;
    FILE *f;
    long i, wrote = 0, readf = 0;
    int err;
    snd_pcm_uframes_t pp = period, pb = buffer, cp = period, cb = buffer;

    /* Playback runs LONGER than capture. If playback runs dry while capture is
     * still going, the underrun stops the linked group and capture overruns
     * with it -- which is what the first working version did, at frame 95760
     * of 96000. The tail is silence; it exists only to keep the group fed. */
    playframes = nframes + 4 * (long)buffer;

    play = calloc((size_t)playframes * CH * 3, 1);
    cap  = calloc((size_t)nframes * CH * 3, 1);
    if (!play || !cap) { fprintf(stderr, "oom\n"); return 1; }

    /* Hann-windowed burst, RIGHT channel only (frame offset 1 of 2). */
    for (i = 0; i < blen; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / (double)(blen - 1));
        double v = 0.8 * w * sin(2.0 * M_PI * bhz * (double)i / (double)rate);
        long fr = burstat + i;
        if (fr >= 0 && fr < playframes)
            put24(play + ((size_t)fr * CH + 1) * 3, v);
    }

    if ((err = snd_pcm_open(&ph, dev, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "open playback %s: %s\n", dev, snd_strerror(err)); return 1; }
    if ((err = snd_pcm_open(&ch_, dev, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "open capture %s: %s\n", dev, snd_strerror(err)); return 1; }
    if ((err = setup(ph, rate, &pp, &pb)) < 0) {
        fprintf(stderr, "setup playback: %s\n", snd_strerror(err)); return 1; }
    if ((err = setup(ch_, rate, &cp, &cb)) < 0) {
        fprintf(stderr, "setup capture: %s\n", snd_strerror(err)); return 1; }

    if ((err = snd_pcm_link(ph, ch_)) < 0) {
        fprintf(stderr, "LINK FAILED: %s -- measurement would be invalid\n",
                snd_strerror(err));
        return 2;
    }

    fprintf(stderr, "dev=%s rate=%u period=%lu buffer=%lu linked=yes\n",
            dev, rate, (unsigned long)pp, (unsigned long)pb);

    /* The linked pair must both be PREPARED before the single trigger. Doing
     * this explicitly, and reporting it, because -EBADFD out of snd_pcm_start
     * says only "wrong state" and not which stream was in it. */
    if ((err = snd_pcm_prepare(ph)) < 0)
        fprintf(stderr, "prepare playback: %s\n", snd_strerror(err));
    if ((err = snd_pcm_prepare(ch_)) < 0)
        fprintf(stderr, "prepare capture: %s\n", snd_strerror(err));
    fprintf(stderr, "state before prefill: play=%s cap=%s\n",
            snd_pcm_state_name(snd_pcm_state(ph)),
            snd_pcm_state_name(snd_pcm_state(ch_)));

    /* Prefill one buffer so the first trigger does not underrun. These frames
     * are playback frames 0..pb-1, which is the origin the answer is measured
     * against. */
    while (wrote < (long)pb && wrote < playframes) {
        snd_pcm_sframes_t n = snd_pcm_writei(ph, play + (size_t)wrote * CH * 3,
                                             (snd_pcm_uframes_t)(pb - wrote));
        if (n < 0) { fprintf(stderr, "prefill: %s\n", snd_strerror((int)n)); return 1; }
        wrote += n;
    }

    fprintf(stderr, "state before start:  play=%s cap=%s\n",
            snd_pcm_state_name(snd_pcm_state(ph)),
            snd_pcm_state_name(snd_pcm_state(ch_)));

    /* The prefill normally auto-starts the group: ALSA clamps the "never"
     * start threshold set above to the buffer boundary, so filling the buffer
     * trips it. That is harmless HERE and only here, because the pair is
     * linked -- one trigger started both, which is the only property the
     * measurement rests on. Playback frame 0 is still the first frame queued,
     * and capture frame 0 is still the same instant.
     *
     * So start explicitly only if the group is somehow still PREPARED. Calling
     * snd_pcm_start on a RUNNING stream returns -EBADFD, which is what the
     * first version of this program did. */
    if (snd_pcm_state(ph) == SND_PCM_STATE_PREPARED) {
        if ((err = snd_pcm_start(ph)) < 0) {
            fprintf(stderr, "start: %s\n", snd_strerror(err));
            return 1;
        }
        fprintf(stderr, "started explicitly\n");
    } else {
        fprintf(stderr, "already running from prefill (linked trigger)\n");
    }
    if (snd_pcm_state(ch_) != SND_PCM_STATE_RUNNING) {
        fprintf(stderr, "CAPTURE NOT RUNNING (%s) -- link did not hold, "
                        "measurement would be invalid\n",
                snd_pcm_state_name(snd_pcm_state(ch_)));
        return 2;
    }
    fprintf(stderr, "state after start:   play=%s cap=%s\n",
            snd_pcm_state_name(snd_pcm_state(ph)),
            snd_pcm_state_name(snd_pcm_state(ch_)));

    while (readf < nframes) {
        snd_pcm_uframes_t want = pp;
        snd_pcm_sframes_t n;
        if ((long)want > nframes - readf) want = (snd_pcm_uframes_t)(nframes - readf);
        n = snd_pcm_readi(ch_, cap + (size_t)readf * CH * 3, want);
        if (n == -EPIPE) {
            /* Recovering would call snd_pcm_prepare, which resets the stream
             * and with it the shared time origin -- every frame index after
             * that point measures from a different zero. There is no honest
             * way to continue, so this is fatal. */
            fprintf(stderr, "OVERRUN at frame %ld -- measurement INVALID\n", readf);
            return 3;
        }
        if (n < 0) { fprintf(stderr, "readi: %s\n", snd_strerror((int)n)); return 1; }
        readf += n;

        if (wrote < playframes) {
            snd_pcm_uframes_t w = pp;
            if ((long)w > playframes - wrote) w = (snd_pcm_uframes_t)(playframes - wrote);
            n = snd_pcm_writei(ph, play + (size_t)wrote * CH * 3, w);
            if (n == -EPIPE) {
                fprintf(stderr, "UNDERRUN at frame %ld -- measurement INVALID\n", wrote);
                return 3;
            }
            if (n < 0) { fprintf(stderr, "writei: %s\n", snd_strerror((int)n)); return 1; }
            wrote += n;
        }
    }

    f = fopen(outpath, "wb");
    if (!f) { perror("fopen"); return 1; }
    fwrite(cap, 1, (size_t)nframes * CH * 3, f);
    fclose(f);

    /* Everything the analyser needs to turn indices into a latency. */
    printf("PERIOD %lu\nBUFFER %lu\nRATE %u\nFRAMES %ld\nBURST_AT %ld\n"
           "BURST_LEN %ld\nBURST_HZ %.1f\nOUT %s\n",
           (unsigned long)pp, (unsigned long)pb, rate, nframes,
           burstat, blen, bhz, outpath);

    snd_pcm_close(ph); snd_pcm_close(ch_);
    free(play); free(cap);
    return 0;
}
