## ADDED Requirements

### Requirement: Game state to shape table conversion
The rendering module SHALL convert the full game state (grid plants, zombies, projectiles, cursor, HUD) into shape table entries and write them to the FPGA via the kernel driver each frame.

#### Scenario: Full frame render
- **WHEN** the game loop completes a state update
- **THEN** the renderer SHALL write all necessary shape entries and background cells to the FPGA via ioctls

### Requirement: Background grid rendering
The renderer SHALL write background cell colors to create a checkerboard pattern in the game area: alternating dark green (index 1) and light green (index 2) for each of the 4x8 grid cells. Non-grid areas (HUD, status bar) SHALL use appropriate background colors.

#### Scenario: Checkerboard pattern
- **WHEN** the background grid is rendered
- **THEN** cells where (row+col) is even SHALL use color index 1 (dark green) and odd cells SHALL use color index 2 (light green)

### Requirement: Plant rendering as shapes
Each planted peashooter SHALL be rendered as 2 shape entries: a body rectangle (green, color index 7) and a stem/base rectangle (dark green, color index 8), positioned within the plant's grid cell.

#### Scenario: Peashooter shape entries
- **WHEN** a peashooter exists at grid cell (r, c)
- **THEN** the renderer SHALL write shape entries for the body and stem at pixel positions derived from (c*80, 60+r*90)

### Requirement: Zombie rendering as shapes
Each active zombie SHALL be rendered as 2 shape entries: a body rectangle (red, color index 5) and a head rectangle/circle (dark red, color index 6).

#### Scenario: Zombie shape entries
- **WHEN** a zombie is active at row r with x_pixel x
- **THEN** the renderer SHALL write shape entries for body and head at the zombie's pixel position

### Requirement: Projectile rendering as shapes
Each active projectile SHALL be rendered as 1 shape entry: a small filled circle (bright green, color index 9).

#### Scenario: Pea shape entry
- **WHEN** a pea is active at row r with x_pixel x
- **THEN** the renderer SHALL write a circle shape entry at the pea's pixel position

### Requirement: Cursor rendering
The cursor SHALL be rendered as a highlighted rectangle outline (yellow, color index 4) around the currently selected grid cell.

#### Scenario: Cursor highlight
- **WHEN** the cursor is at grid cell (r, c)
- **THEN** the renderer SHALL write a shape entry for a yellow border rectangle at the cell's pixel coordinates

### Requirement: HUD rendering
The HUD area (y=0-59) SHALL display the sun counter as 7-segment digit shapes (white, color index 10) and a plant card indicator.

#### Scenario: Sun counter digits
- **WHEN** the sun count is 125
- **THEN** the renderer SHALL write 3 digit shape entries displaying "1", "2", "5"

### Requirement: Shape table index allocation
The renderer SHALL allocate shape table indices deterministically: fixed ranges for plants (0-31), zombies (32-41), projectiles (42-47 or overlapping), cursor, and HUD elements. Unused entries SHALL have visible=0.

#### Scenario: Unused entries hidden
- **WHEN** fewer than 48 entities exist
- **THEN** unused shape table entries SHALL be written with visible=0
