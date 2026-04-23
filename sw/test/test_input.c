/*
 * Test program: print keyboard events from /dev/input/eventX.
 *
 * Usage: ./test_input [/dev/input/eventN]
 * Default: /dev/input/event0
 *
 * How it fits in:
 *   On-board smoke test for the input pipeline (design-doc section
 *   "On-Board Integration"). Runs the same polling loop as main.c,
 *   input_poll every 16.67 ms, but instead of feeding the game it
 *   prints which logical key fired. If the output looks right, every
 *   stage of the keyboard path is working: the evdev device is
 *   readable, O_NONBLOCK polling at 60 Hz catches events, and the
 *   KEY_* -> INPUT_* mapping in input.c matches what the user
 *   pressed.
 *
 * ESC exits cleanly (closes the fd and returns 0). Any other key
 * prints its name and the loop keeps going. Ctrl+C also works (tty
 * default).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../input.h"

int main(int argc, char *argv[])
{
    const char *dev_path = "/dev/input/event0";
    if (argc > 1)
        dev_path = argv[1];

    printf("Opening %s for keyboard events...\n", dev_path);
    printf("Press arrow keys, space, D, ESC. Ctrl+C to quit.\n\n");

    if (input_init(dev_path) < 0) {
        fprintf(stderr, "Failed to open %s\n", dev_path);
        return 1;
    }

    for (;;) {
        int key = input_poll();
        switch (key) {
        case INPUT_UP:    printf("UP\n"); break;
        case INPUT_DOWN:  printf("DOWN\n"); break;
        case INPUT_LEFT:  printf("LEFT\n"); break;
        case INPUT_RIGHT: printf("RIGHT\n"); break;
        case INPUT_SPACE: printf("SPACE (place plant)\n"); break;
        case INPUT_D:     printf("D (remove plant)\n"); break;
        case INPUT_ESC:   printf("ESC (quit)\n"); input_close(); return 0;
        case INPUT_NONE:  break;
        }
        // 1_000_000 us / 60 ~= 16667; matches main.c's frame budget.
        usleep(16667); /* ~60 Hz polling */
    }

    input_close();
    return 0;
}
