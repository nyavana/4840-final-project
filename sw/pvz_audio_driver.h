#ifndef _PVZ_AUDIO_DRIVER_H
#define _PVZ_AUDIO_DRIVER_H

#include <linux/ioctl.h>

#define PVZ_AUDIO_MAGIC      'A'

/*
 * BGM_CTRL: arg is 0 (stop) or 1 (play).
 * PLAY_SFX: arg is a cue id in [1..8]; driver round-robins between the two
 *           voice registers. Argument 0 stops whichever voice last played.
 * STATUS:   readback of {sfx1_active, sfx0_active, bgm_active} as uint32_t.
 */
#define PVZ_AUDIO_BGM_CTRL   _IOW(PVZ_AUDIO_MAGIC, 1, uint32_t)
#define PVZ_AUDIO_PLAY_SFX   _IOW(PVZ_AUDIO_MAGIC, 2, uint32_t)
#define PVZ_AUDIO_STATUS     _IOR(PVZ_AUDIO_MAGIC, 3, uint32_t)

#endif /* _PVZ_AUDIO_DRIVER_H */
