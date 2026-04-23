#ifndef PVZ_INPUT_H
#define PVZ_INPUT_H

/*
 * Unified input action enum.
 *
 * The input module auto-detects whether a USB gamepad (BTN_SOUTH capable,
 * e.g. Xbox 360 via xpad) or a keyboard is present at startup and
 * translates device-specific events into these action codes.  Game
 * code (main.c) branches only on input_action_t, never on device type.
 */
typedef enum {
    INPUT_NONE = 0,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_PLACE,
    INPUT_DIG,
    INPUT_CYCLE_PREV,
    INPUT_CYCLE_NEXT,
    INPUT_QUIT
} input_action_t;

/*
 * Enumerate /dev/input/event0..31, pick the first gamepad if one is
 * advertising BTN_SOUTH, else the first keyboard advertising KEY_SPACE.
 * The chosen device path is logged to stdout and stderr.
 *
 * Returns 0 on success, -1 if no suitable device was found.
 */
int input_init(void);

/*
 * Poll for one input action.  Returns INPUT_NONE if no event is pending.
 * Only key-press events (value == 1) produce actions; release and repeat
 * events are ignored.
 */
input_action_t input_poll(void);

/*
 * Release the input device.
 */
void input_close(void);

/*
 * Returns 1 if a gamepad is the active input source, 0 if a keyboard,
 * -1 if input has not been initialised.  Used by the enumeration
 * helper test_input_devices.
 */
int input_is_gamepad(void);

#endif /* PVZ_INPUT_H */
