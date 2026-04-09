## ADDED Requirements

### Requirement: Read keyboard events from /dev/input/eventX
The input module SHALL open and read keyboard events from a Linux input device (`/dev/input/eventX`) using non-blocking I/O or a separate thread.

#### Scenario: Key press detection
- **WHEN** the user presses a key on the keyboard
- **THEN** the input module SHALL detect the key press event within one game loop iteration (16.7 ms)

#### Scenario: No blocking on no input
- **WHEN** no keys are pressed during a frame
- **THEN** the input module SHALL return immediately without blocking the game loop

### Requirement: Arrow key cursor movement
The input module SHALL map arrow key presses (KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT) to cursor movement on the 4x8 grid.

#### Scenario: Move cursor right
- **WHEN** the right arrow key is pressed and cursor column < 7
- **THEN** the cursor column SHALL increase by 1

#### Scenario: Cursor boundary clamping
- **WHEN** an arrow key would move the cursor outside the grid bounds (row 0-3, col 0-7)
- **THEN** the cursor position SHALL remain unchanged

### Requirement: Space key places plant
The input module SHALL map the space key to plant placement at the current cursor position.

#### Scenario: Space triggers placement
- **WHEN** the space key is pressed
- **THEN** the game logic SHALL attempt to place a peashooter at the current cursor cell

### Requirement: D key removes plant
The input module SHALL map the D key to plant removal at the current cursor position.

#### Scenario: D triggers removal
- **WHEN** the D key is pressed
- **THEN** the game logic SHALL attempt to remove the plant at the current cursor cell

### Requirement: ESC key quits game
The input module SHALL map the ESC key to game exit.

#### Scenario: ESC quits
- **WHEN** the ESC key is pressed
- **THEN** the game loop SHALL terminate cleanly
