/*
 * Test program: echo resolved actions from the auto-detected input
 * device.  Use on-board with controller and/or keyboard plugged in.
 *
 * Usage: ./test_input
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../input.h"

int main(void)
{
    if (input_init() < 0) {
        fprintf(stderr, "No gamepad or keyboard found\n");
        return 1;
    }

    printf("Device ready (%s).  Press Start/ESC to quit.\n",
           input_is_gamepad() ? "gamepad" : "keyboard");

    for (;;) {
        input_action_t action = input_poll();
        switch (action) {
        case INPUT_UP:         printf("UP\n"); break;
        case INPUT_DOWN:       printf("DOWN\n"); break;
        case INPUT_LEFT:       printf("LEFT\n"); break;
        case INPUT_RIGHT:      printf("RIGHT\n"); break;
        case INPUT_PLACE:      printf("PLACE\n"); break;
        case INPUT_DIG:        printf("DIG\n"); break;
        case INPUT_CYCLE_PREV: printf("CYCLE_PREV\n"); break;
        case INPUT_CYCLE_NEXT: printf("CYCLE_NEXT\n"); break;
        case INPUT_QUIT:       printf("QUIT\n"); input_close(); return 0;
        case INPUT_NONE:       break;
        }
        usleep(16667); /* ~60 Hz polling */
    }

    input_close();
    return 0;
}
