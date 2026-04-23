## ADDED Requirements

### Requirement: Shape index allocation for v3
`render.c` SHALL use the following shape index ranges:

| Indices | Entity |
|---|---|
| 0–31 | Plants (1 slot per grid cell, `r * 8 + c`) |
| 32–36 | Zombies (5 slots) |
| 37–42 | Peas (6 slots, same budget as v2) |
| 43 | Plant-card selection highlight (lower index than cards so the card paints on top, leaving a visible border) |
| 44–46 | Plant cards (Peashooter, Sunflower, Wall-nut) |
| 47–50 | Sun counter digits (up to 4 digits, max 9999 sun) |
| 51–62 | Reserved (future: zombies-remaining counter, spawn-rate indicator, per-zombie accessory shapes) |
| 63 | Cursor (highest index, always draws on top) |

Reserved slots SHALL be explicitly hidden each frame (`visible = 0`) so stale data from a prior frame never reaches the FPGA.

#### Scenario: All plant slots used
- **WHEN** `render_plants` runs for a full 4 × 8 grid
- **THEN** shape slots 0..31 SHALL carry one entry per cell (visible = 1 if plant present, else 0)

#### Scenario: Cursor on top
- **WHEN** any shape overlaps with the cursor's screen region
- **THEN** the cursor's shape at slot 63 SHALL draw on top because it has the highest index

### Requirement: Type-aware plant rendering
`render_plants` SHALL branch on `p->type` for each non-empty cell:

| Plant type | Shape type | Size | Color (palette idx) | Cell-local offset |
|---|---|---|---|---|
| Peashooter | `SHAPE_SPRITE` | 64 × 64 | – (sprite ROM) | (8, 13) |
| Sunflower | `SHAPE_CIRCLE` | 60 × 60 | 4 (yellow) | (10, 15) |
| Wall-nut | `SHAPE_CIRCLE` | 70 × 70 | 3 (brown) | (5, 10) |

#### Scenario: Sunflower circle visible
- **WHEN** a Sunflower occupies cell (r, c)
- **THEN** shape slot `r * 8 + c` SHALL be a visible yellow circle at the listed offset

#### Scenario: Wall-nut circle visible
- **WHEN** a Wall-nut occupies cell (r, c)
- **THEN** shape slot `r * 8 + c` SHALL be a visible brown circle at the listed offset

#### Scenario: Empty cell hidden
- **WHEN** cell (r, c) has `type == PLANT_NONE`
- **THEN** shape slot `r * 8 + c` SHALL have `visible == 0`

### Requirement: Type-aware zombie rendering
`render_zombies` SHALL render each active zombie as a 30 × 70 rectangle colored by zombie type:

| Zombie type | Palette index | Color |
|---|---|---|
| Basic | 5 | red |
| Conehead | 12 | orange |
| Buckethead | 11 | gray |

#### Scenario: Conehead orange
- **WHEN** an active zombie has `type == ZOMBIE_CONEHEAD`
- **THEN** its shape SHALL be rendered with palette index 12

#### Scenario: Buckethead gray
- **WHEN** an active zombie has `type == ZOMBIE_BUCKETHEAD`
- **THEN** its shape SHALL be rendered with palette index 11

### Requirement: Plant-card HUD rendering
`render_hud` SHALL emit three plant-card shapes (slots 44–46) and one selection-highlight shape (slot 43) every frame while `state == STATE_PLAYING`. When the state is not `STATE_PLAYING`, all four SHALL be hidden.

#### Scenario: Highlight position tracks selection
- **WHEN** `selected_plant == i`
- **THEN** the highlight shape's x SHALL equal `10 + i * 65 - 2`
- **AND** its y SHALL equal `3`

#### Scenario: Overlay hides HUD
- **WHEN** the game state becomes `STATE_WIN` or `STATE_LOSE`
- **THEN** the plant cards and highlight SHALL all have `visible == 0`

### Requirement: Widened shape-table usage
`render.c` SHALL assume the FPGA shape table has 64 entries (indices 0..63). Writes to indices ≥ 48 SHALL succeed when the driver and FPGA have been built with the v3 shape-table size.

#### Scenario: Write to high index
- **WHEN** `render.c` writes a shape at index 63
- **THEN** the kernel driver SHALL accept the write
- **AND** the FPGA SHALL render the shape during the next frame
