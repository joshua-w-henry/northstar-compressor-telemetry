# CAN Notes

## Known signal

### RPM

- Extended CAN ID: `0x0C665500`
- Data bytes: 2-3
- Byte order: big-endian
- Formula: `rpm = (data[2] << 8) | data[3]`

Observed ranges from compressor testing:

- starter/cranking: roughly 100-600 RPM
- idle-command behavior: roughly 2100-2200 RPM
- normal loaded operation: roughly 2700-2800 RPM

Treat these as observations, not protocol limits.

## Vacuum goals

For every observed CAN ID retain:

- extended/standard flag
- DLC
- frame count
- first/last seen time
- inter-arrival/frequency statistics
- last payload
- byte-change masks
- byte min/max where useful
