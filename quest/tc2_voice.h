#pragma once
/* Voice chat between linked cabinets (docs/ONLINE.md): 48 kHz mono, Opus in 20 ms frames, gated by voice activity, sent as extra
 * network frames beside the arcade link and mixed into the game's own audio output (Android allows one output device). */
#include <stdbool.h>
#include <stdint.h>

void tc2_voice_init(void);
void tc2_voice_set_active(bool linked);           /* capture runs only while linked to a partner */
void tc2_voice_set_enabled(bool on);
void tc2_voice_set_muted(bool muted);
void tc2_voice_set_volume(int percent);           /* the partner's voice, 0..200 */
bool tc2_voice_enabled(void);
bool tc2_voice_muted(void);
int  tc2_voice_volume(void);

void tc2_voice_tick(void);                        /* once per arcade frame: encode what was captured, decode what arrived */
bool tc2_voice_pending(void);                     /* encoded voice waits to be sent */
int  tc2_voice_pack(uint8_t *out);                /* 1 = out holds a 256-byte voice frame */
bool tc2_voice_is_packet(const uint8_t *in);
void tc2_voice_unpack(const uint8_t *in);
int  tc2_voice_mix(void);                         /* one sample at the game's 88.2 kHz output rate (added to both channels) */

/* Capture feed: SDL's capture callback on desktop, MainActivity's voice-communication recorder on Quest (any thread). */
void tc2_voice_capture(const int16_t *pcm,int samples);
void tc2_voice_test_output(const char *wav_path);   /* tests: record what is played (TC2_VOICE_OUT, or online-test.txt on a headset) */

typedef struct tc2_voice_status { bool active,capturing,talking,partner_talking,mic_denied; int level,partner_level; uint32_t sent,received,lost; } tc2_voice_status;
void tc2_voice_get_status(tc2_voice_status *out);
