## ADDED Requirements

### Requirement: VGA timing generation at 640x480@60Hz
The system SHALL generate VGA timing signals (hsync, vsync, hcount, vcount, blanking) conforming to the 640x480@60Hz VGA standard using a 25.175 MHz pixel clock.

#### Scenario: Correct horizontal timing
- **WHEN** the VGA timing generator is running
- **THEN** hcount SHALL cycle 0-799, hsync SHALL assert low during the sync pulse (656-751), and horizontal blanking SHALL be active for hcount >= 640

#### Scenario: Correct vertical timing
- **WHEN** the VGA timing generator is running
- **THEN** vcount SHALL cycle 0-524, vsync SHALL assert low during the sync pulse (490-491), and vertical blanking SHALL be active for vcount >= 480

### Requirement: Dual linebuffer with hsync swap
The system SHALL implement two 640-entry x 8-bit line RAMs. One buffer SHALL be written (draw buffer) while the other is read (display buffer). Buffers SHALL swap roles on each hsync.

#### Scenario: Write to draw buffer while reading display buffer
- **WHEN** the display engine is rendering scanline N
- **THEN** the shape renderer SHALL write pixels for scanline N+1 into the draw buffer while the VGA output reads scanline N from the display buffer

#### Scenario: Swap on hsync
- **WHEN** hsync occurs (end of active scanline)
- **THEN** the draw and display buffer roles SHALL swap within 2 pixel clock cycles

### Requirement: Background grid rendering
The system SHALL maintain a 4-row x 8-column background color grid. Each cell SHALL map to an 80x90 pixel region within the game area (y=60 to y=419). The grid SHALL be rendered as the first layer into the draw buffer at the start of each scanline.

#### Scenario: Correct cell-to-pixel mapping
- **WHEN** rendering pixel at coordinate (x, y) where y is in range [60, 419]
- **THEN** the background color SHALL come from grid cell (row=(y-60)/90, col=x/80)

#### Scenario: Background fill timing
- **WHEN** a new scanline begins rendering (hcount == 0 of the draw cycle)
- **THEN** the background grid SHALL fill all 640 pixels of the draw buffer within 640 clock cycles

### Requirement: Shape table with 48 entries
The system SHALL maintain a shape table of 48 entries. Each entry SHALL store: type (2 bits: 0=rectangle, 1=circle, 2=seven-segment digit), visible flag (1 bit), x position (10 bits), y position (9 bits), width (9 bits), height (9 bits), and color index (8 bits).

#### Scenario: Shape storage capacity
- **WHEN** software writes 48 shape entries
- **THEN** all 48 entries SHALL be stored and available for rendering

#### Scenario: Shape types supported
- **WHEN** a shape entry has type 0, 1, or 2
- **THEN** the renderer SHALL draw a filled rectangle, filled circle, or 7-segment digit respectively

### Requirement: Shadow/active double-buffering at vsync
All shape table entries and background grid cells SHALL be written to shadow registers. Shadow registers SHALL be latched into active registers atomically at the start of vertical blanking (vcount == 480, hcount == 0).

#### Scenario: No mid-frame tearing
- **WHEN** software updates a shape entry during active display
- **THEN** the update SHALL NOT be visible until the next vsync latch

#### Scenario: Atomic latch at vsync
- **WHEN** vcount == 480 and hcount == 0
- **THEN** all shadow registers SHALL be copied to active registers in a single clock cycle

### Requirement: Shape renderer iterates all visible shapes per scanline
For each scanline, the shape renderer SHALL iterate through all 48 shape table entries (from the active table) and write pixels to the draw buffer for any visible shape that overlaps the current scanline. Later shapes in the table SHALL overwrite earlier shapes (painter's algorithm).

#### Scenario: Filled rectangle rendering
- **WHEN** a visible rectangle shape has bounds that overlap the current scanline
- **THEN** the renderer SHALL write the shape's color index to the draw buffer for all pixels where (x <= px < x+width) AND (y <= scanline < y+height)

#### Scenario: Filled circle rendering
- **WHEN** a visible circle shape overlaps the current scanline
- **THEN** the renderer SHALL write the shape's color index for all pixels where (px-cx)^2 + (py-cy)^2 <= radius^2, with cx=x+width/2, cy=y+height/2, radius=width/2

#### Scenario: Seven-segment digit rendering
- **WHEN** a visible digit shape (type 2) is on the current scanline
- **THEN** the renderer SHALL draw the digit value (encoded in width[3:0]) as a 7-segment pattern at position (x, y) using the shape's color index

#### Scenario: Invisible shapes skipped
- **WHEN** a shape entry has visible == 0
- **THEN** the renderer SHALL skip that entry without writing any pixels

### Requirement: Color palette lookup
The system SHALL provide a 256-entry color palette LUT mapping 8-bit color indices to 24-bit RGB values. The palette SHALL be hardcoded with at least 13 colors for MVP use (black, two greens, brown, yellow, red, dark red, green, dark green, bright green, white, gray, orange).

#### Scenario: Palette output
- **WHEN** the display buffer outputs an 8-bit color index during active display
- **THEN** the color palette SHALL convert it to 24-bit RGB for VGA output within 1 clock cycle

### Requirement: Rendering completes within scanline budget
The full rendering pipeline (background fill + shape iteration) SHALL complete within the available cycles per scanline (~1600 pixel clocks) so that the draw buffer is ready before the next hsync swap.

#### Scenario: Worst-case 48 shapes
- **WHEN** all 48 shape entries are visible and overlap the current scanline
- **THEN** rendering SHALL complete before the hsync swap deadline
