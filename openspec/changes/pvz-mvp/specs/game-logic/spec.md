## ADDED Requirements

### Requirement: 4x8 game grid with cell state
The game SHALL maintain a 4-row x 8-column grid. Each cell SHALL track whether it is empty or contains a plant (type and fire cooldown).

#### Scenario: Initial grid state
- **WHEN** the game starts
- **THEN** all 32 grid cells SHALL be empty

#### Scenario: Plant placement
- **WHEN** the player places a peashooter at an empty cell and has >= 50 sun
- **THEN** the cell SHALL contain a peashooter and sun SHALL decrease by 50

#### Scenario: Plant placement blocked
- **WHEN** the player attempts to place a plant at an occupied cell or with < 50 sun
- **THEN** the placement SHALL be rejected and the grid SHALL remain unchanged

### Requirement: Zombie entities with HP and movement
The game SHALL support up to 5 active zombie entities. Each zombie SHALL have a row, x pixel position, HP (starting at 3), and movement state. Zombies SHALL move leftward at approximately 20 pixels/second (1 pixel every 3 frames at 60 fps).

#### Scenario: Zombie movement
- **WHEN** a zombie's move counter reaches ZOMBIE_SPEED_FRAMES (3)
- **THEN** the zombie's x position SHALL decrease by 1 pixel and the move counter SHALL reset

#### Scenario: Zombie death
- **WHEN** a zombie's HP reaches 0
- **THEN** the zombie SHALL be deactivated and removed from the game

### Requirement: Zombie spawning
The game SHALL spawn zombies at random intervals between 8-15 seconds, in a random row (0-3), entering from the right edge of the screen. Total zombies per level SHALL be 5.

#### Scenario: Spawn timing
- **WHEN** the spawn timer elapses and zombies_spawned < 5
- **THEN** a new zombie SHALL be spawned in a random row at x = right edge (639) with HP = 3

#### Scenario: No over-spawning
- **WHEN** zombies_spawned == 5
- **THEN** no more zombies SHALL be spawned

### Requirement: Projectile entities
The game SHALL support up to 16 active projectile (pea) entities. Each pea SHALL have a row and x pixel position, moving rightward at 2 pixels per frame (~120 px/sec).

#### Scenario: Pea movement
- **WHEN** each frame updates
- **THEN** each active pea's x position SHALL increase by PEA_SPEED (2) pixels

#### Scenario: Off-screen removal
- **WHEN** a pea's x position exceeds 639
- **THEN** the pea SHALL be deactivated

### Requirement: Peashooter firing
Each peashooter SHALL fire a pea when there is at least one active zombie in the same row and the plant's fire cooldown has elapsed (2-second interval).

#### Scenario: Fire when zombie in row
- **WHEN** a peashooter's cooldown reaches 0 and a zombie exists in its row
- **THEN** a new pea SHALL be spawned at the peashooter's grid position and the cooldown SHALL reset to 120 frames (2 seconds)

#### Scenario: No fire when no zombie in row
- **WHEN** no zombie exists in a peashooter's row
- **THEN** the peashooter SHALL NOT fire, but its cooldown MAY still count down

### Requirement: Collision detection
The game SHALL check for pea-zombie collisions each frame. A collision occurs when a pea's bounding region overlaps a zombie's bounding region on the same row.

#### Scenario: Pea hits zombie
- **WHEN** a pea and zombie overlap on the same row
- **THEN** the zombie's HP SHALL decrease by 1 (PEA_DAMAGE) and the pea SHALL be deactivated

### Requirement: Sun economy
The game SHALL track a sun counter starting at 100. Sun SHALL increase by 25 every 8 seconds. Placing a peashooter SHALL cost 50 sun.

#### Scenario: Passive sun income
- **WHEN** 8 seconds elapse
- **THEN** sun SHALL increase by 25

#### Scenario: Sun deduction on plant
- **WHEN** a peashooter is placed
- **THEN** sun SHALL decrease by 50

### Requirement: Win condition
The player SHALL win when all 5 zombies have been spawned and all spawned zombies are dead (HP <= 0).

#### Scenario: Win
- **WHEN** zombies_spawned == 5 AND no active zombies remain
- **THEN** the game SHALL enter the win state

### Requirement: Lose condition
The player SHALL lose when any zombie's x pixel position reaches 0 (left edge of screen).

#### Scenario: Lose
- **WHEN** any active zombie has x_pixel <= 0
- **THEN** the game SHALL enter the lose state

### Requirement: Plant removal
The player SHALL be able to remove a plant at the cursor position by pressing D.

#### Scenario: Remove plant
- **WHEN** the player presses D with the cursor on a cell containing a plant
- **THEN** the cell SHALL become empty (no sun refund)
