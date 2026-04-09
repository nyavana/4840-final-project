## ADDED Requirements

### Requirement: Avalon-MM agent with 5 registers
The `pvz_top` module SHALL implement an Avalon-MM agent (slave) on the lightweight HPS-to-FPGA bridge with 32-bit writedata and 5 word-aligned registers at byte offsets 0x00, 0x04, 0x08, 0x0C, and 0x10.

#### Scenario: Register decode
- **WHEN** the CPU writes to byte offset 0x00, 0x04, 0x08, 0x0C, or 0x10
- **THEN** the Avalon agent SHALL decode the address and route the write to the corresponding register (BG_CELL, SHAPE_ADDR, SHAPE_DATA0, SHAPE_DATA1, SHAPE_COMMIT)

#### Scenario: Avalon write timing
- **WHEN** `chipselect` and `write_n` are asserted
- **THEN** the agent SHALL accept `writedata` on the same clock cycle with zero wait states

### Requirement: BG_CELL register (offset 0x00)
Writing to BG_CELL SHALL update one background grid cell. Bit fields: [2:0] column (0-7), [4:3] row (0-3), [12:8] color index.

#### Scenario: Write background cell
- **WHEN** the CPU writes 0x00000902 to offset 0x00
- **THEN** the shadow background grid cell at row=0, col=2 SHALL be set to color index 9

### Requirement: SHAPE_ADDR register (offset 0x04)
Writing to SHAPE_ADDR SHALL select the shape table entry index for subsequent SHAPE_DATA0/DATA1 writes. Bit field: [5:0] shape index (0-47).

#### Scenario: Select shape entry
- **WHEN** the CPU writes 0x00000005 to offset 0x04
- **THEN** subsequent DATA0 and DATA1 writes SHALL target shape entry 5

### Requirement: SHAPE_DATA0 register (offset 0x08)
Writing to SHAPE_DATA0 SHALL set word 0 of the currently addressed shape entry. Bit fields: [1:0] type, [2] visible, [12:3] x position, [21:13] y position.

#### Scenario: Write shape descriptor word 0
- **WHEN** the CPU writes a value to offset 0x08
- **THEN** the shape type, visible flag, x, and y fields of the selected shadow shape entry SHALL be updated

### Requirement: SHAPE_DATA1 register (offset 0x0C)
Writing to SHAPE_DATA1 SHALL set word 1 of the currently addressed shape entry. Bit fields: [8:0] width, [17:9] height, [25:18] color index.

#### Scenario: Write shape descriptor word 1
- **WHEN** the CPU writes a value to offset 0x0C
- **THEN** the width, height, and color fields of the selected shadow shape entry SHALL be updated

### Requirement: SHAPE_COMMIT register (offset 0x10)
Writing any nonzero value to SHAPE_COMMIT SHALL finalize the currently addressed shape entry into the shadow table (making DATA0 and DATA1 values take effect for the next vsync latch).

#### Scenario: Commit shape entry
- **WHEN** the CPU writes 0x00000001 to offset 0x10
- **THEN** the shape entry at the current SHAPE_ADDR SHALL be committed to the shadow table

### Requirement: Byte-addressed CPU access with word-aligned Avalon
The register interface SHALL use 32-bit Avalon writedata. The CPU SHALL access registers at byte offsets (base + 4*N). The Avalon address port SHALL use word offsets internally.

#### Scenario: CPU byte offset maps to Avalon word offset
- **WHEN** the CPU writes to byte offset 0x08 (SHAPE_DATA0)
- **THEN** the Avalon agent SHALL see address word offset 2 and decode it as SHAPE_DATA0
