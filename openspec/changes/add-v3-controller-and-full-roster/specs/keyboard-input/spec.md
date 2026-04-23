## ADDED Requirements

### Requirement: Bracket keys cycle plant selection
The keyboard backend SHALL map `KEY_LEFTBRACE` (`[`) to `INPUT_CYCLE_PREV` and `KEY_RIGHTBRACE` (`]`) to `INPUT_CYCLE_NEXT`.

#### Scenario: Left bracket cycles backward
- **WHEN** the `[` key is pressed
- **THEN** `input_poll` SHALL return `INPUT_CYCLE_PREV`

#### Scenario: Right bracket cycles forward
- **WHEN** the `]` key is pressed
- **THEN** `input_poll` SHALL return `INPUT_CYCLE_NEXT`

## MODIFIED Requirements

### Requirement: Arrow key cursor movement
The input module SHALL map arrow key presses (KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT) to the unified `input_action_t` enum values `INPUT_UP`, `INPUT_DOWN`, `INPUT_LEFT`, `INPUT_RIGHT`. The caller in `main.c` SHALL clamp the cursor to the grid bounds, not `input.c`.

#### Scenario: Up arrow returns INPUT_UP
- **WHEN** `KEY_UP` is pressed
- **THEN** `input_poll` SHALL return `INPUT_UP`

#### Scenario: Down arrow returns INPUT_DOWN
- **WHEN** `KEY_DOWN` is pressed
- **THEN** `input_poll` SHALL return `INPUT_DOWN`

#### Scenario: Left arrow returns INPUT_LEFT
- **WHEN** `KEY_LEFT` is pressed
- **THEN** `input_poll` SHALL return `INPUT_LEFT`

#### Scenario: Right arrow returns INPUT_RIGHT
- **WHEN** `KEY_RIGHT` is pressed
- **THEN** `input_poll` SHALL return `INPUT_RIGHT`

### Requirement: Space key places plant
The space key SHALL map to `INPUT_PLACE`. The actual plant type placed is determined by `gs->selected_plant` in `main.c`, not by `input.c`.

#### Scenario: Space returns INPUT_PLACE
- **WHEN** `KEY_SPACE` is pressed
- **THEN** `input_poll` SHALL return `INPUT_PLACE`

### Requirement: D key removes plant
The D key SHALL map to `INPUT_DIG`.

#### Scenario: D returns INPUT_DIG
- **WHEN** `KEY_D` is pressed
- **THEN** `input_poll` SHALL return `INPUT_DIG`

### Requirement: ESC key quits game
The ESC key SHALL map to `INPUT_QUIT`.

#### Scenario: ESC returns INPUT_QUIT
- **WHEN** `KEY_ESC` is pressed
- **THEN** `input_poll` SHALL return `INPUT_QUIT`
