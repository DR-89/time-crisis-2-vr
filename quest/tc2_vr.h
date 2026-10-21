#pragma once
#include <stdbool.h>
#include "eng/ss22_input.h"
#include "tc2_build.h"
bool tc2_vr_open(void);
bool ss22_host_frame(void);
void ss22_host_close(void);
void ss22_input_neutral(void);
void ss22_input_update(void);
void tc2_audio_open(void);
void tc2_audio_volume(int percent);
extern uint16_t tc2_inputs, g_ss22_gun_x, g_ss22_gun_y;
extern bool g_ss22_gun_off;
struct QCoverSpot;
/* The arcade cover slide for the camera the host is about to draw (tc2_window.c). */
void qvr_set_cover_spot(const struct QCoverSpot *spot);
/* Opens LAN Play over the paused game (the arcade SOLO/LINKED shot, tc2_window.c). */
void qvr_open_lan_from_arcade(void);
void qtc2_draw(const float view[16], const float projection[16], int width, int height, unsigned framebuffer);
