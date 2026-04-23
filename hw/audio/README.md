# Audio asset pipeline

Source audio lives in `assets/`. The generator `build_audio_mif.py` reads
`manifest.json`, validates format, and emits three files at the project root
(`hw/bgm_rom.mem`, `hw/sfx_rom.mem`, `hw/sfx_offsets.svh`) — all three are
committed to git.

## Required asset format

8 kHz, 16-bit, mono WAV. The generator refuses any other format. Pre-process
your source audio before dropping it here:

```
sox input.mp3 -r 8000 -c 1 -b 16 output.wav
```

Or in Audacity: Tracks → Resample → 8000 Hz; Tracks → Mix → Stereo to Mono;
File → Export → WAV (Microsoft signed 16-bit PCM).

## Regenerating after a change

```
make audio   # (from hw/)
# or directly:
python3 audio/build_audio_mif.py
```

Then `git add hw/bgm_rom.mem hw/sfx_rom.mem hw/sfx_offsets.svh hw/audio/assets/<changed>.wav`
and commit — the three generated files are committed alongside the asset.

## Missing assets

If a SFX WAV is absent, the generator fills its slot with
`silent_samples_per_missing_cue` zero samples (default 2400 = 0.3 s) rather
than failing. This is how Scope A ships: with none of the SFX WAVs present,
`sfx_rom.mem` is 19200 zero-valued samples, the voice modules play silence,
and the end-to-end hardware path is still fully exercised.

## BGM

Scope A ships with a placeholder tone-sweep `bgm_theme.wav` (220 Hz → 880 Hz
over 15 s). Replace it with a real arrangement of the PvZ theme — same
8 kHz / 16-bit / mono format — and re-run the generator.
