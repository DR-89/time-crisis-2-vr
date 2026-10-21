#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { QL_UP,QL_DOWN,QL_LEFT,QL_RIGHT,QL_ACCEPT,QL_BACK } QLobbyKey;
void qlobby_show(void);
bool qlobby_visible(void);
bool qlobby_editing(void);
const char *qlobby_text_value(void);
void qlobby_replace_text(const char *text);
void qlobby_keyboard_error(const char *message);
unsigned qlobby_keyboard_request(void);
void qlobby_text(const char *text);
void qlobby_backspace(void);
void qlobby_keyboard_result(bool requested);
void qlobby_input(QLobbyKey key);
/* 1 resume, -1 pause for an error, 0 unchanged; polls even while paused. */
int qlobby_tick(uint64_t milliseconds);
unsigned qlobby_revision(void);
void qlobby_render(uint8_t pixels[640*480*4]);
