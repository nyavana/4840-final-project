#ifndef PVZ_INPUT_H
#define PVZ_INPUT_H

/* Input event codes returned by input_poll().
 * Phase E rewrites this into an input_action_t enum; these #defines are
 * the transition form used by Phase D wiring. */
#define INPUT_NONE         0
#define INPUT_UP           1
#define INPUT_DOWN         2
#define INPUT_LEFT         3
#define INPUT_RIGHT        4
#define INPUT_SPACE        5
#define INPUT_D            6
#define INPUT_ESC          7
#define INPUT_CYCLE_PREV   8
#define INPUT_CYCLE_NEXT   9
/* Phase E synonyms used by the new unified action space */
#define INPUT_PLACE        INPUT_SPACE
#define INPUT_DIG          INPUT_D
#define INPUT_QUIT         INPUT_ESC

/*
 * Initialize keyboard input from a Linux input device.
 * Returns 0 on success, -1 on failure.
 */
int input_init(const char *device_path);

/*
 * Poll for a keyboard event (non-blocking).
 * Returns one of the INPUT_* codes, or INPUT_NONE if no key pressed.
 */
int input_poll(void);

/*
 * Close the input device.
 */
void input_close(void);

#endif /* PVZ_INPUT_H */
