#ifndef MBOXFW_STREAMING_H
#define MBOXFW_STREAMING_H

void streaming_set_rate(unsigned long hz);
void streaming_playback_enable(unsigned char on);
void streaming_capture_enable(unsigned char on);
void streaming_sof(void);

#endif
