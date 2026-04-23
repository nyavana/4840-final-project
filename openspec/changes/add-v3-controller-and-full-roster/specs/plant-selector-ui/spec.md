## ADDED Requirements

### Requirement: Plant cards along the top HUD
The game SHALL render three plant cards along the top HUD row: Peashooter (green, palette index 7), Sunflower (yellow, palette index 4), Wall-nut (brown, palette index 3). Cards SHALL be drawn as filled rectangles, 55 × 45 pixels, positioned at y=5 with x = `10 + i * 65` for i ∈ {0, 1, 2}.

#### Scenario: Cards always visible during play
- **WHEN** the game state is `STATE_PLAYING`
- **THEN** all three plant cards SHALL be rendered every frame

#### Scenario: Cards hidden on game over
- **WHEN** the game state is `STATE_WIN` or `STATE_LOSE`
- **THEN** the plant cards SHALL have `visible = 0` so the overlay is not painted over

### Requirement: Selection highlight marks the active card
The game state SHALL carry an integer `selected_plant` in `{0, 1, 2}` indicating the active card. A yellow highlight rectangle 59 × 49 pixels SHALL be drawn two pixels up and two pixels left of the active card, at a lower shape index than the cards so that each card paints over the highlight where they overlap and the remaining visible yellow forms a 2-pixel border around the selected card.

#### Scenario: Initial selection
- **WHEN** the game starts
- **THEN** `selected_plant` SHALL equal 0 (Peashooter)

#### Scenario: Highlight tracks selection
- **WHEN** `selected_plant` changes from `i` to `j`
- **THEN** the highlight's x position SHALL move to the card at index `j` within one frame

### Requirement: Plant-selection cycling inputs
`INPUT_CYCLE_PREV` SHALL decrement `selected_plant` modulo 3. `INPUT_CYCLE_NEXT` SHALL increment `selected_plant` modulo 3. Both actions SHALL be accepted from either the keyboard (`[` / `]`) or the gamepad (LB / RB) regardless of which device is active.

#### Scenario: Cycle forward wraps
- **WHEN** `selected_plant == 2` and `INPUT_CYCLE_NEXT` is received
- **THEN** `selected_plant` SHALL become 0

#### Scenario: Cycle backward wraps
- **WHEN** `selected_plant == 0` and `INPUT_CYCLE_PREV` is received
- **THEN** `selected_plant` SHALL become 2

#### Scenario: No-op on cycle during game over
- **WHEN** the game state is `STATE_WIN` or `STATE_LOSE`
- **THEN** `selected_plant` SHALL NOT change regardless of cycle inputs

### Requirement: Placement uses the selected plant type
When `INPUT_PLACE` is received, `game_place_plant` SHALL attempt to place a plant of the type selected by `selected_plant` at the cursor cell, subject to the per-type cost.

#### Scenario: Place Sunflower when selected
- **WHEN** `selected_plant == 1` and the player presses the place action with an empty cell and sufficient sun
- **THEN** the cursor cell SHALL contain a Sunflower and sun SHALL decrease by that plant's cost

#### Scenario: Place Wall-nut when selected
- **WHEN** `selected_plant == 2` and the player presses the place action with an empty cell and sufficient sun
- **THEN** the cursor cell SHALL contain a Wall-nut and sun SHALL decrease by that plant's cost
