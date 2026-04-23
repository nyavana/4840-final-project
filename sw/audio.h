#ifndef PVZ_AUDIO_H
#define PVZ_AUDIO_H

#include "pvz.h"   /* pvz_sfx_cue_t */

/* Opens /dev/pvz_audio; returns 0 on success, -1 on failure. */
int  audio_init(void);

/* Idempotent start/stop of background music. */
void audio_bgm_start(void);
void audio_bgm_stop(void);

/* Fire a one-shot SFX. Safe to call with PVZ_SFX_NONE (no-op). */
void audio_play_sfx(pvz_sfx_cue_t cue);

/* Close device handle. */
void audio_close(void);

#endif /* PVZ_AUDIO_H */
