## ADDED Requirements

### Requirement: Plant type enum
`game.h` SHALL expose an enum of plant types: `PLANT_NONE`, `PLANT_PEASHOOTER`, `PLANT_SUNFLOWER`, `PLANT_WALLNUT`.

#### Scenario: Default plant is none
- **WHEN** the grid is zero-initialised at game start
- **THEN** every cell's `plant_t.type` SHALL equal `PLANT_NONE`

### Requirement: Per-type plant stats
Each plant type SHALL have a fixed max HP and sun cost.

| Type | Max HP | Cost |
|---|---|---|
| Peashooter | 3 | 50 |
| Sunflower | 2 | 50 |
| Wall-nut | 8 | 50 |

#### Scenario: Peashooter HP on place
- **WHEN** a Peashooter is placed
- **THEN** its HP field SHALL equal 3

#### Scenario: Sunflower HP on place
- **WHEN** a Sunflower is placed
- **THEN** its HP field SHALL equal 2

#### Scenario: Wall-nut HP on place
- **WHEN** a Wall-nut is placed
- **THEN** its HP field SHALL equal 8

#### Scenario: Placement cost deducted by type
- **WHEN** any plant is placed
- **THEN** `gs->sun` SHALL decrease by that plant's cost

### Requirement: Plant action timer dispatches by type
The `fire_cooldown` field on `plant_t` SHALL be repurposed as a generic per-plant action timer. A single update pass SHALL iterate the grid and branch on plant type:

- **Peashooter**: when the timer reaches 0 and a zombie exists in the same row, a pea SHALL spawn and the timer SHALL reset to `PEASHOOTER_FIRE_COOLDOWN` (120 frames).
- **Sunflower**: when the timer reaches 0, `gs->sun` SHALL increase by 25 and the timer SHALL reset to `SUNFLOWER_PRODUCE_COOLDOWN` (600 frames).
- **Wall-nut**: the timer SHALL remain at 0 and no action SHALL occur.

#### Scenario: Peashooter fires when zombie in row
- **WHEN** a Peashooter's timer reaches 0 and an active zombie is in its row
- **THEN** a pea SHALL be spawned and the timer SHALL reset to 120

#### Scenario: Peashooter does not fire with no zombie
- **WHEN** a Peashooter's timer reaches 0 and no zombie is in its row
- **THEN** no pea SHALL be spawned
- **AND** the timer MAY stay at 0 until a zombie arrives

#### Scenario: Sunflower produces sun on timer
- **WHEN** a Sunflower's timer reaches 0
- **THEN** `gs->sun` SHALL increase by 25 regardless of zombies present
- **AND** the timer SHALL reset to 600

#### Scenario: Wall-nut never fires
- **WHEN** a Wall-nut is present on the grid for any number of frames
- **THEN** no pea SHALL be spawned from that cell
- **AND** `gs->sun` SHALL NOT increase from that cell

### Requirement: Zombie type enum and HP table
`game.h` SHALL expose an enum of zombie types: `ZOMBIE_BASIC`, `ZOMBIE_CONEHEAD`, `ZOMBIE_BUCKETHEAD`. Each type SHALL have a fixed max HP: 3 / 6 / 12 respectively.

#### Scenario: Basic zombie HP
- **WHEN** a Basic zombie is spawned
- **THEN** its HP field SHALL equal 3

#### Scenario: Conehead zombie HP
- **WHEN** a Conehead zombie is spawned
- **THEN** its HP field SHALL equal 6

#### Scenario: Buckethead zombie HP
- **WHEN** a Buckethead zombie is spawned
- **THEN** its HP field SHALL equal 12

#### Scenario: Collision damage is uniform
- **WHEN** a pea collides with any zombie type
- **THEN** the zombie's HP SHALL decrease by `PEA_DAMAGE` (1)
- **AND** the zombie SHALL die when HP reaches 0

### Requirement: Expanded zombie count per level
`TOTAL_ZOMBIES` SHALL equal 10. `MAX_ZOMBIES` (concurrent active zombies) SHALL stay at 5.

#### Scenario: Win after ten zombies
- **WHEN** `gs->zombies_spawned == 10` and every active zombie is dead
- **THEN** the game SHALL enter `STATE_WIN`

### Requirement: Selected plant placement
`game_place_plant` SHALL read `gs->selected_plant` and place the plant type that corresponds to that index: 0 → Peashooter, 1 → Sunflower, 2 → Wall-nut. Cost and HP SHALL come from the per-type table above.

#### Scenario: Place peashooter with selection 0
- **WHEN** `selected_plant == 0`, the cursor is on an empty cell, and sun ≥ 50
- **THEN** a Peashooter SHALL be placed and sun SHALL decrease by 50

#### Scenario: Place sunflower with selection 1
- **WHEN** `selected_plant == 1`, the cursor is on an empty cell, and sun ≥ 50
- **THEN** a Sunflower SHALL be placed and sun SHALL decrease by 50

#### Scenario: Place wall-nut with selection 2
- **WHEN** `selected_plant == 2`, the cursor is on an empty cell, and sun ≥ 50
- **THEN** a Wall-nut SHALL be placed and sun SHALL decrease by 50

#### Scenario: Placement blocked by cost
- **WHEN** `selected_plant` is any value and sun < that plant's cost
- **THEN** placement SHALL fail and no change SHALL occur

#### Scenario: Placement blocked by occupied cell
- **WHEN** the cursor is on a cell whose plant type is not `PLANT_NONE`
- **THEN** placement SHALL fail and no change SHALL occur
