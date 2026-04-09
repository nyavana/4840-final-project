/*
 * Test program: print keyboard events from /dev/input/eventX
 *
 * Usage: ./test_input [/dev/input/eventN]
 * Default: /dev/input/event0
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
        usleep(16667); /* ~60 Hz polling */
    }

    input_close();
    return 0;
}
