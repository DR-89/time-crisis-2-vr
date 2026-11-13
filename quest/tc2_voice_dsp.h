#pragma once
/* The microphone's clean-up before voice chat encodes it (tc2_voice.c): echo cancellation (SpeexDSP's MDF filter) against what
 * this app plays, then noise suppression with a speech probability (RNNoise). 48 kHz mono, 10 ms blocks.
 *
 * Both sides are placed on one clock, in 48 kHz samples: the game's output by when it will leave the audio queue, the microphone
 * by when it arrived. The echo of a played sample reaches the microphone some time later (the device's buffers, the air, the
 * recorder's buffers); the filter learns that delay and the room, up to VDSP_TAIL_MS. */
#include <stdbool.h>
#include <stdint.h>

#define VDSP_RATE    48000
#define VDSP_BLOCK   480                 /* 10 ms */
#define VDSP_TAIL_MS 250

void vdsp_init(void);
void vdsp_reset(void);                   /* a new session: forget the echo path and both timelines */
void vdsp_set_options(bool echo_cancel,bool denoise);
void vdsp_set_lock(void (*lock)(void),void (*unlock)(void));   /* the played-audio store is shared by two threads */

/* The game's output as it is queued: 88.2 kHz stereo frames, the first leaving the queue at play_at; queue_was_empty: the device
 * ran out before this block (the game was held), so it leaves at once, after a gap. */
void vdsp_played(const int16_t *stereo,int frames,int64_t play_at,bool queue_was_empty);
/* Microphone samples, the last of them arriving at now. Each cleaned block goes to out with its speech probability (0..1). */
typedef void (*vdsp_out)(const int16_t *block,int n,float speech);
void vdsp_capture(const int16_t *mic,int n,int64_t now,vdsp_out out);

/* Tests: add what was played back into the microphone, gain and delay_ms later (a speaker heard by the microphone). */
void vdsp_test_echo(float gain,int delay_ms);
