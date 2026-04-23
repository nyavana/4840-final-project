## MODIFIED Requirements

### Requirement: 64-entry shape table with shadow/active double-buffering
The FPGA SHALL store up to 64 shape entries in a shadow/active register file. Writes land in shadow immediately; the entire shadow SHALL latch to active on the single cycle where `vcount == 480` and `hcount == 0`.

#### Scenario: Write during active video does not tear
- **WHEN** software writes new shape data while `vcount < 480`
- **THEN** the active registers SHALL NOT change until the next vertical-blank boundary

#### Scenario: All 64 entries render
- **WHEN** software populates all 64 active shape entries, all visible
- **THEN** `shape_renderer.sv` SHALL visit every entry each scanline
- **AND** the highest-indexed entry whose bounding box contains a pixel SHALL determine that pixel's color

#### Scenario: Address range accepted
- **WHEN** software writes to `SHAPE_ADDR = 63`
- **THEN** the write SHALL be decoded into the 64th shadow entry

## ADDED Requirements

### Requirement: Testbench coverage for high-index entries
`hw/tb/tb_shape_renderer.sv` and `hw/tb/tb_pvz_top.sv` SHALL include at least one case that writes a shape at an index ≥ 48 and verifies the rendered pixel output.

#### Scenario: Mid-range index rendered
- **WHEN** `tb_pvz_top` writes a shape at index 50
- **THEN** the testbench SHALL observe the expected RGB at the pixel coordinate declared in the case

#### Scenario: Top index rendered on top
- **WHEN** `tb_shape_renderer` writes two overlapping shapes at indices 40 and 63
- **THEN** the observed pixel color at the overlap SHALL match the shape at index 63
