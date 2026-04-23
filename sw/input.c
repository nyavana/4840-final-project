/*
 * Input auto-detect between USB gamepad and keyboard (v3)
 *
 * Enumerates /dev/input/event0..31 at startup, queries each device's
 * EV_KEY capability bitmap, and opens the first device that advertises
 * BTN_SOUTH (treated as an Xbox 360-compatible gamepad).  If no gamepad
 * is found, falls back to the first device advertising KEY_SPACE.
 *
 * input_poll() returns a unified input_action_t enum so game code never
 * branches on device type.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include "input.h"

#define INPUT_MAX_DEVICES 32

/* Bitmap helpers for EVIOCGBIT result */
#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, arr) \
    (((arr)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1UL)

static int input_fd = -1;
static int is_gamepad = -1;
static char input_path[64] = {0};

/* Returns 1 if the device's EV_KEY capability set includes `code`. */
static int has_key_cap(int fd, int code)
{
    unsigned long bits[NBITS(KEY_MAX + 1)];
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
        return 0;
    return TEST_BIT(code, bits) ? 1 : 0;
}

int input_init(void)
{
    int gamepad_fd = -1;
    int keyboard_fd = -1;
    char gamepad_path[64] = {0};
    char keyboard_path[64] = {0};

    for (int i = 0; i < INPUT_MAX_DEVICES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        int is_gp = has_key_cap(fd, BTN_SOUTH);
        int is_kb = has_key_cap(fd, KEY_SPACE);

        if (is_gp && gamepad_fd < 0) {
            gamepad_fd = fd;
            snprintf(gamepad_path, sizeof(gamepad_path), "%s", path);
        } else if (is_kb && !is_gp && keyboard_fd < 0) {
            keyboard_fd = fd;
            snprintf(keyboard_path, sizeof(keyboard_path), "%s", path);
        } else {
            close(fd);
        }
    }

    if (gamepad_fd >= 0) {
        if (keyboard_fd >= 0)
            close(keyboard_fd);
        input_fd = gamepad_fd;
        is_gamepad = 1;
        snprintf(input_path, sizeof(input_path), "%s", gamepad_path);
        fprintf(stdout, "input: using gamepad at %s\n", input_path);
        fprintf(stderr, "input: using gamepad at %s\n", input_path);
        return 0;
    }

    if (keyboard_fd >= 0) {
        input_fd = keyboard_fd;
        is_gamepad = 0;
        snprintf(input_path, sizeof(input_path), "%s", keyboard_path);
        fprintf(stdout, "input: no gamepad found, using keyboard at %s\n",
                input_path);
        fprintf(stderr, "input: no gamepad found, using keyboard at %s\n",
                input_path);
        return 0;
    }

    fprintf(stderr, "input: no gamepad or keyboard detected\n");
    return -1;
}

static input_action_t translate_gamepad(const struct input_event *ev)
{
    if (ev->type == EV_KEY && ev->value == 1) {
        switch (ev->code) {
        case BTN_DPAD_UP:    return INPUT_UP;
        case BTN_DPAD_DOWN:  return INPUT_DOWN;
        case BTN_DPAD_LEFT:  return INPUT_LEFT;
        case BTN_DPAD_RIGHT: return INPUT_RIGHT;
        case BTN_SOUTH:      return INPUT_PLACE;
        case BTN_EAST:       return INPUT_DIG;
        case BTN_TL:         return INPUT_CYCLE_PREV;
        case BTN_TR:         return INPUT_CYCLE_NEXT;
        case BTN_START:      return INPUT_QUIT;
        default: break;
        }
    } else if (ev->type == EV_ABS) {
        /* Some xpad flavours report the D-pad as absolute hat axes */
        if (ev->code == ABS_HAT0Y) {
            if (ev->value < 0) return INPUT_UP;
            if (ev->value > 0) return INPUT_DOWN;
        } else if (ev->code == ABS_HAT0X) {
            if (ev->value < 0) return INPUT_LEFT;
            if (ev->value > 0) return INPUT_RIGHT;
        }
    }
    return INPUT_NONE;
}

static input_action_t translate_keyboard(const struct input_event *ev)
{
    if (ev->type != EV_KEY || ev->value != 1)
        return INPUT_NONE;
    switch (ev->code) {
    case KEY_UP:         return INPUT_UP;
    case KEY_DOWN:       return INPUT_DOWN;
    case KEY_LEFT:       return INPUT_LEFT;
    case KEY_RIGHT:      return INPUT_RIGHT;
    case KEY_SPACE:      return INPUT_PLACE;
    case KEY_D:          return INPUT_DIG;
    case KEY_LEFTBRACE:  return INPUT_CYCLE_PREV;
    case KEY_RIGHTBRACE: return INPUT_CYCLE_NEXT;
    case KEY_ESC:        return INPUT_QUIT;
    default:             return INPUT_NONE;
    }
}

input_action_t input_poll(void)
{
    struct input_event ev;

    if (input_fd < 0)
        return INPUT_NONE;

    while (read(input_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        input_action_t action = is_gamepad
            ? translate_gamepad(&ev)
            : translate_keyboard(&ev);
        if (action != INPUT_NONE)
            return action;
    }

    return INPUT_NONE;
}

void input_close(void)
{
    if (input_fd >= 0) {
        close(input_fd);
        input_fd = -1;
    }
    is_gamepad = -1;
    input_path[0] = '\0';
}

int input_is_gamepad(void)
{
    return is_gamepad;
}
