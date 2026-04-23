#include "audio.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "pvz_audio_driver.h"

static int audio_fd = -1;

int audio_init(void) {
    audio_fd = open("/dev/pvz_audio", O_RDWR);
    if (audio_fd < 0) {
        perror("audio_init: open /dev/pvz_audio");
        return -1;
    }
    return 0;
}

void audio_close(void) {
    if (audio_fd >= 0) {
        close(audio_fd);
        audio_fd = -1;
    }
}

void audio_bgm_start(void) {
    uint32_t v = 1;
    if (audio_fd >= 0 && ioctl(audio_fd, PVZ_AUDIO_BGM_CTRL, &v) < 0)
        perror("audio_bgm_start");
}

void audio_bgm_stop(void) {
    uint32_t v = 0;
    if (audio_fd >= 0 && ioctl(audio_fd, PVZ_AUDIO_BGM_CTRL, &v) < 0)
        perror("audio_bgm_stop");
}

void audio_play_sfx(pvz_sfx_cue_t cue) {
    if (cue == PVZ_SFX_NONE) return;
    uint32_t v = (uint32_t)cue;
    if (audio_fd >= 0 && ioctl(audio_fd, PVZ_AUDIO_PLAY_SFX, &v) < 0)
        perror("audio_play_sfx");
}
