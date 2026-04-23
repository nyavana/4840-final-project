## ADDED Requirements

### Requirement: Gamepad auto-detection at startup
The input module SHALL enumerate `/dev/input/event0..31` at startup, classify each device by its advertised `EV_KEY` capability bits, and open the first device that advertises `BTN_SOUTH` as the active input source. This covers Xbox 360–compatible controllers driven by the Linux `xpad` driver.

#### Scenario: Controller present
- **WHEN** at least one evdev device advertises `BTN_SOUTH`
- **THEN** the input module SHALL open that device and record it as the active input source
- **AND** the input module SHALL log the chosen device path to stdout on startup

#### Scenario: Controller not present
- **WHEN** no evdev device advertises `BTN_SOUTH`
- **THEN** the input module SHALL fall back to the first device that advertises `KEY_SPACE`
- **AND** the input module SHALL log the fallback to the console

#### Scenario: No input device at all
- **WHEN** neither a gamepad nor a keyboard is found
- **THEN** `input_init` SHALL return a non-zero error code
- **AND** `main.c` SHALL print a clear error and exit with status 1

### Requirement: Xbox 360 button mapping to unified actions
When a gamepad is the active input source, the input module SHALL translate controller button presses into the shared `input_action_t` enum.

#### Scenario: D-pad up
- **WHEN** the controller's d-pad up (`BTN_DPAD_UP` or `ABS_HAT0Y = -1`) is pressed
- **THEN** `input_poll` SHALL return `INPUT_UP`

#### Scenario: D-pad down
- **WHEN** the controller's d-pad down is pressed
- **THEN** `input_poll` SHALL return `INPUT_DOWN`

#### Scenario: D-pad left
- **WHEN** the controller's d-pad left is pressed
- **THEN** `input_poll` SHALL return `INPUT_LEFT`

#### Scenario: D-pad right
- **WHEN** the controller's d-pad right is pressed
- **THEN** `input_poll` SHALL return `INPUT_RIGHT`

#### Scenario: A (south) button places plant
- **WHEN** `BTN_SOUTH` is pressed
- **THEN** `input_poll` SHALL return `INPUT_PLACE`

#### Scenario: B (east) button removes plant
- **WHEN** `BTN_EAST` is pressed
- **THEN** `input_poll` SHALL return `INPUT_DIG`

#### Scenario: Left shoulder cycles plant selection backward
- **WHEN** `BTN_TL` is pressed
- **THEN** `input_poll` SHALL return `INPUT_CYCLE_PREV`

#### Scenario: Right shoulder cycles plant selection forward
- **WHEN** `BTN_TR` is pressed
- **THEN** `input_poll` SHALL return `INPUT_CYCLE_NEXT`

#### Scenario: Start button quits
- **WHEN** `BTN_START` is pressed
- **THEN** `input_poll` SHALL return `INPUT_QUIT`

### Requirement: Only key-press events count
The input module SHALL act on key-press events only (evdev `value == 1`). Key-release and auto-repeat events SHALL be ignored so that cursor movement and plant placement are one-shot per physical press.

#### Scenario: Release ignored
- **WHEN** the device emits a key-release event (`value == 0`)
- **THEN** `input_poll` SHALL return `INPUT_NONE` for that event

#### Scenario: Repeat ignored
- **WHEN** the device emits a key-repeat event (`value == 2`)
- **THEN** `input_poll` SHALL return `INPUT_NONE` for that event

### Requirement: On-board enumeration helper
A standalone program `sw/test/test_input_devices` SHALL print a human-readable report of every `/dev/input/event*` device, the capabilities it advertises relevant to the game, and which device the auto-detect would pick.

#### Scenario: Run with controller and keyboard
- **WHEN** `test_input_devices` runs with a controller and a keyboard both plugged in
- **THEN** the output SHALL list both devices with capability summaries
- **AND** the "chosen" device SHALL be the one advertising `BTN_SOUTH`
