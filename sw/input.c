/*
 * Keyboard input via /dev/input/eventX.
 *
 * Non-blocking I/O reads key-press events from a Linux input
 * device. Arrow keys, space, D, and ESC map to game actions.
 *
 * How it fits in:
 *   Called from main.c's process_input each frame, and from the
 *   test/test_input.c harness. Public surface (INPUT_* codes and the
 *   three functions) lives in input.h. See design-doc section
 *   "Input System" and its controller-mapping table.
 *
 * Two input paths, only one implemented:
 *   The design calls for a USB gamepad read via libusb
 *   (libusb_interrupt_transfer polling an HID interrupt endpoint,
 *   with the report descriptor laying out the D-pad nibble and
 *   A/B/L/R/Start button bits) plus a keyboard fallback via
 *   /dev/input/eventX using O_NONBLOCK read(). The MVP only has the
 *   keyboard fallback. No vendor/product match, no interface claim,
 *   no kernel-driver detach, no HID report parse. Adding those is
 *   easy later because the output shape (INPUT_* codes returned by
 *   input_poll) is already decoupled from the source.
 *
 * /dev/input/eventX:
 *   The kernel's evdev subsystem exposes each input device as a
 *   file that emits struct input_event records. With O_NONBLOCK,
 *   read() returns EAGAIN instead of blocking when no events are
 *   queued, which is what a 60 Hz polling loop wants. Each event
 *   has type/code/value; EV_KEY with value == 1 is "key down",
 *   0 is "up", 2 is "auto-repeat". We keep only value == 1, per the
 *   "only key-down events" rule in the design doc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include "input.h"

static int input_fd = -1;

/*
 * Open the evdev node for the keyboard. O_NONBLOCK is required;
 * input_poll relies on read() returning immediately when no events
 * are queued. The right device node varies per board - usually
 * /dev/input/event0 on the DE1-SoC, but the caller can override.
 */
int input_init(const char *device_path)
{
    input_fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (input_fd < 0) {
        perror("input_init: open");
        return -1;
    }
    return 0;
}

/*
 * Drain pending events and return the first recognized key press.
 * Unknown codes and non-key-down events (releases, repeats, EV_SYN
 * delimiters, EV_MSC) get skipped, so one call keeps reading until
 * it finds something useful or the kernel buffer runs dry (read
 * returns EAGAIN / short read).
 */
int input_poll(void)
{
    struct input_event ev;

    if (input_fd < 0)
        return INPUT_NONE;

    while (read(input_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        /* Only handle key press events (value == 1) */
        // EV_KEY + value==1 is key-down. value==0 is release,
        // value==2 is auto-repeat; we drop both.
        if (ev.type != EV_KEY || ev.value != 1)
            continue;

        /* Keyboard -> game-action mapping. Matches the "Keyboard
         * Fallback" column in the design-doc controller table. */
        switch (ev.code) {
        case KEY_UP:    return INPUT_UP;
        case KEY_DOWN:  return INPUT_DOWN;
        case KEY_LEFT:  return INPUT_LEFT;
        case KEY_RIGHT: return INPUT_RIGHT;
        case KEY_SPACE: return INPUT_SPACE;   // place plant (= gamepad A)
        case KEY_D:     return INPUT_D;       // remove plant (= gamepad B)
        case KEY_ESC:   return INPUT_ESC;     // quit (= gamepad Start)
        default:        break;
        }
    }

    return INPUT_NONE;
}

void input_close(void)
{
    if (input_fd >= 0) {
        close(input_fd);
        input_fd = -1;
    }
}
