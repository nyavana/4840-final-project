/*
 * test_audio — standalone smoke test for /dev/pvz_audio.
 * Usage:
 *   ./test_audio            — play BGM for 3 s, stop, exit.
 *   ./test_audio --sfx N    — trigger SFX cue N (1..8), then exit.
 */
#include "../audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (audio_init() < 0) {
        fprintf(stderr, "audio_init failed\n");
        return 1;
    }

    if (argc >= 3 && strcmp(argv[1], "--sfx") == 0) {
        int cue = atoi(argv[2]);
        if (cue < 1 || cue > 8) {
            fprintf(stderr, "sfx cue must be 1..8\n");
            audio_close();
            return 1;
        }
        audio_play_sfx((pvz_sfx_cue_t)cue);
        sleep(1);
    } else {
        printf("Starting BGM for 3 seconds...\n");
        audio_bgm_start();
        sleep(3);
        audio_bgm_stop();
        printf("BGM stopped.\n");
    }

    audio_close();
    return 0;
}
