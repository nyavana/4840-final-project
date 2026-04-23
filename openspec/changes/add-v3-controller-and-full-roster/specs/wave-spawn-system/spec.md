## ADDED Requirements

### Requirement: Precomputed wave table per level
The game SHALL maintain a compile-time template of 10 wave entries, each a `(spawn_frame, type)` pair. At `game_init`, the template SHALL be copied into `gs->wave[]` and each entry's `spawn_frame` SHALL be adjusted by a uniform random jitter in the range `[-60, +60]` frames. `gs->wave_index` SHALL track the next entry to spawn.

#### Scenario: Template applied at init
- **WHEN** `game_init` runs
- **THEN** `gs->wave[]` SHALL hold 10 entries in the same order as the template
- **AND** each entry's `spawn_frame` SHALL differ from the template's by at most 60 frames

#### Scenario: Fixed seed produces deterministic wave
- **WHEN** `srand(seed)` is called with a fixed seed before `game_init`
- **THEN** the resulting `gs->wave[]` values SHALL be reproducible across runs

### Requirement: Spawn from the table instead of random intervals
`update_spawning` SHALL compare `gs->frame_count` to `gs->wave[gs->wave_index].spawn_frame`. When the frame counter meets or exceeds the scheduled frame, the function SHALL spawn a zombie of the specified type and advance `gs->wave_index`.

#### Scenario: Basic zombie at scheduled frame
- **WHEN** `frame_count >= wave[wave_index].spawn_frame` and `wave[wave_index].type == ZOMBIE_BASIC`
- **THEN** a zombie slot SHALL be set active with `type = ZOMBIE_BASIC`, HP = 3, row chosen randomly, x = right edge

#### Scenario: Conehead zombie at scheduled frame
- **WHEN** the entry's type is `ZOMBIE_CONEHEAD`
- **THEN** the spawned zombie SHALL have HP = 6

#### Scenario: Buckethead zombie at scheduled frame
- **WHEN** the entry's type is `ZOMBIE_BUCKETHEAD`
- **THEN** the spawned zombie SHALL have HP = 12

#### Scenario: All waves consumed
- **WHEN** `wave_index` equals 10
- **THEN** `update_spawning` SHALL be a no-op
- **AND** subsequent frames SHALL NOT spawn any more zombies

### Requirement: Fallback when all zombie slots are busy
If the wave table calls for a spawn but every `zombie_t` slot is currently active, the spawn SHALL be held and retried on the next frame. `wave_index` SHALL NOT advance until the spawn succeeds.

#### Scenario: Slot contention
- **WHEN** a scheduled spawn frame is reached and all `MAX_ZOMBIES` slots are active
- **THEN** no zombie SHALL be spawned this frame
- **AND** `wave_index` SHALL remain unchanged
- **AND** the next frame with a free slot SHALL receive the held spawn
