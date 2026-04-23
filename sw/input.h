#ifndef PVZ_INPUT_H
#define PVZ_INPUT_H

/*
 * Public input surface for the game.
 *
 * How it fits in:
 *   main.c calls input_init once at startup, input_poll once per
 *   frame, and input_close at shutdown. test/test_input.c exercises
 *   the same three functions. See the "Input System" section of the
 *   design document.
 *
 * The INPUT_* constants are source-agnostic action codes; swapping
 * the implementation from keyboard evdev to a libusb gamepad read
 * would not touch this header.
 */

/* Input event codes returned by input_poll() */
#define INPUT_NONE   0
#define INPUT_UP     1
#define INPUT_DOWN   2
#define INPUT_LEFT   3
#define INPUT_RIGHT  4
#define INPUT_SPACE  5
#define INPUT_D      6
#define INPUT_ESC    7

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
