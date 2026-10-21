#pragma once
#include <stdbool.h>
/* No renderer/system-event access. This bridge owns only its keyboard overlay. */
bool qovr_keyboard_show(const char *initial);
/* 0 pending, 1 accepted (replacement text), -1 cancelled, -2 runtime lost. */
int qovr_keyboard_poll(char text[16]);
void qovr_keyboard_cancel(void);
void qovr_keyboard_shutdown(void);
const char *qovr_keyboard_error(void);
