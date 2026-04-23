## MODIFIED Requirements

### Requirement: PVZ_WRITE_SHAPE ioctl accepts indices up to 63
The `PVZ_WRITE_SHAPE` ioctl in `sw/pvz_driver.c` SHALL accept `pvz_shape_arg_t.index` values in `[0, 63]` inclusive. Values outside this range SHALL cause the ioctl to return `-EINVAL`.

#### Scenario: Valid high index accepted
- **WHEN** userspace calls the ioctl with `index = 63`
- **THEN** the driver SHALL write SHAPE_ADDR with 63, SHAPE_DATA0/DATA1 with the shape fields, then SHAPE_COMMIT
- **AND** the ioctl SHALL return 0

#### Scenario: Out-of-range index rejected
- **WHEN** userspace calls the ioctl with `index = 64`
- **THEN** the ioctl SHALL return `-EINVAL`
- **AND** no write SHALL occur on the Avalon bus

### Requirement: Register map unchanged
The Avalon register offsets and bit layouts SHALL be identical to v2Dino. The compatible string SHALL remain `csee4840,pvz_gpu-1.0`.

#### Scenario: Driver probe still binds
- **WHEN** the v3 kernel module is loaded against a v3-synthesised FPGA image
- **THEN** the driver SHALL bind to the device-tree node with compatible string `csee4840,pvz_gpu-1.0`
- **AND** `/dev/pvz` SHALL appear
