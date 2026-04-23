/*
 * On-board helper: enumerate /dev/input/event* and print what the
 * v3 auto-detect would pick.
 *
 * Prints for each accessible event node:
 *   - path
 *   - whether it advertises BTN_SOUTH (gamepad)
 *   - whether it advertises KEY_SPACE (keyboard)
 *   - a tag summarising the classification
 *
 * Then prints the device that `input_init` would choose.
 *
 * Usage on the DE1-SoC (after plugging/unplugging controllers):
 *   ./test_input_devices
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#include "../input.h"

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NBITS(x)        ((((x) - 1) / BITS_PER_LONG) + 1)
#define TEST_BIT(bit, arr) \
    (((arr)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1UL)

static int has_key_cap(int fd, int code)
{
    unsigned long bits[NBITS(KEY_MAX + 1)];
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
        return 0;
    return TEST_BIT(code, bits) ? 1 : 0;
}

static void describe_one(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return;

    char name[128] = "(unknown)";
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);

    int is_gp = has_key_cap(fd, BTN_SOUTH);
    int is_kb = has_key_cap(fd, KEY_SPACE);

    const char *tag;
    if (is_gp)
        tag = "GAMEPAD";
    else if (is_kb)
        tag = "KEYBOARD";
    else
        tag = "other";

    printf("  %s : [%s] name=\"%s\" BTN_SOUTH=%d KEY_SPACE=%d\n",
           path, tag, name, is_gp, is_kb);

    close(fd);
}

int main(void)
{
    printf("Scanning /dev/input/event0..31...\n");
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        if (access(path, R_OK) == 0)
            describe_one(path);
    }

    printf("\nRunning input_init()...\n");
    int rc = input_init();
    if (rc < 0) {
        printf("Result: no suitable device\n");
        return 1;
    }

    if (input_is_gamepad())
        printf("Result: chose GAMEPAD\n");
    else
        printf("Result: chose KEYBOARD\n");

    input_close();
    return 0;
}
