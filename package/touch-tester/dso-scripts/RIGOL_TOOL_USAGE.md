# rigol-tool.sh - Usage Guide

## Overview

`rigol-tool.sh` is a high-level automation tool for Rigol oscilloscopes (DHO-924S and compatible models). It provides commands for configuration management, measurements, and automated testing.

## Phase 1 - Implemented Commands

### 1. check-connection
Test connectivity to the oscilloscope.

```bash
./rigol-tool.sh --command=check-connection
./rigol-tool.sh --command=check-connection --ip=192.168.1.7
```

**Output:**
- ✅ Device identification if connected
- ❌ Troubleshooting steps if connection fails

### 2. status
Display current oscilloscope configuration in human-readable format.

```bash
./rigol-tool.sh --command=status
./rigol-tool.sh --command=status --verbose
```

**Shows:**
- Device information
- Channel status (scale, offset, display on/off)
- Timebase settings
- Trigger configuration

### 3. copy-setup
Save current oscilloscope configuration to JSON file.

```bash
# Save to specific file
./rigol-tool.sh --command=copy-setup --output=my-config.json

# Save to project-specific location
./rigol-tool.sh --command=copy-setup --output=dso-configs/touch-tester/latency-2ch.json
```

**JSON Structure:**
```json
{
  "version": "1.0",
  "timestamp": "2025-11-02T14:30:00Z",
  "device": "RIGOL TECHNOLOGIES,DHO924S,...",
  "channels": {
    "1": {
      "display": true,
      "scale": 1.0,
      "offset": 0.0,
      "coupling": "DC",
      "probe": 1,
      "bandwidth_limit": "OFF"
    },
    ...
  },
  "timebase": {
    "scale": 0.010,
    "offset": 0.0
  },
  "trigger": {
    "source": "CHAN1",
    "level": 1.65,
    "slope": "POS",
    "sweep": "NORM",
    "coupling": "DC"
  }
}
```

### 4. apply-setup
Restore oscilloscope configuration from JSON file.

```bash
# Restore from file
./rigol-tool.sh --command=apply-setup --input=my-config.json

# Restore project-specific config
./rigol-tool.sh --command=apply-setup --input=dso-configs/touch-tester/latency-2ch.json
```

**Process:**
1. Validates JSON file format
2. Applies channel configurations
3. Applies timebase settings
4. Applies trigger configuration
5. Reports success/failure for each step

### 5. query
Send raw SCPI query command (for debugging/advanced use).

```bash
# Query measurement
./rigol-tool.sh --command=query --scpi=":MEAS:NWID? CHAN1"

# Query channel scale
./rigol-tool.sh --command=query --scpi=":CHAN1:SCAL?"

# Get device ID
./rigol-tool.sh --command=query --scpi="*IDN?"
```

### 6. reset
Reset oscilloscope to factory defaults (interactive confirmation).

```bash
./rigol-tool.sh --command=reset
```

**Warning:** This will erase all current settings!

## Global Options

### --ip=IP_ADDRESS
Specify oscilloscope IP address (overrides RIGOL_IP environment variable).

```bash
./rigol-tool.sh --command=status --ip=192.168.1.100
```

**Default:** `192.168.1.7` (or `$RIGOL_IP` if set)

### --port=PORT
Specify SCPI TCP port.

```bash
./rigol-tool.sh --command=status --port=5555
```

**Default:** `5555` (or `$RIGOL_PORT` if set)

### --verbose
Enable verbose debug output (shows all SCPI commands and responses).

```bash
./rigol-tool.sh --command=copy-setup --output=test.json --verbose
```

**Output includes:**
- 🔍 DEBUG: SCPI WRITE: :CHAN1:DISP ON
- 🔍 DEBUG: SCPI QUERY: :CHAN1:SCAL?
- 🔍 DEBUG: SCPI RESPONSE: 1.0000E+00

## Environment Variables

### RIGOL_IP
Set default oscilloscope IP address.

```bash
export RIGOL_IP=192.168.1.7
./rigol-tool.sh --command=status  # Uses 192.168.1.7
```

### RIGOL_PORT
Set default SCPI port.

```bash
export RIGOL_PORT=5555
```

## Workflow Examples

### Example 1: Save and Restore Touch Latency Setup

```bash
# 1. Manually configure scope for touch latency testing:
#    - CH1: GPIO 27 (touch pulse) - 1V/div, 0V offset
#    - CH2: GPIO 7 (probe) - 1V/div, 0V offset
#    - Timebase: 10ms/div
#    - Trigger: CH1, rising edge, 1.65V, normal mode

# 2. Save the configuration
./rigol-tool.sh --command=copy-setup \
    --output=dso-configs/touch-tester/latency-gpio27-gpio7.json

# 3. Later, restore it quickly
./rigol-tool.sh --command=apply-setup \
    --input=dso-configs/touch-tester/latency-gpio27-gpio7.json
```

### Example 2: Verify Configuration Before Testing

```bash
# Check connection
./rigol-tool.sh --command=check-connection

# View current settings
./rigol-tool.sh --command=status

# If wrong, restore correct setup
./rigol-tool.sh --command=apply-setup --input=dso-configs/touch-tester/latency-2ch.json

# Verify it was applied
./rigol-tool.sh --command=status
```

### Example 3: Debugging SCPI Commands

```bash
# Test specific SCPI query
./rigol-tool.sh --command=query --scpi=":MEAS:NWID? CHAN1" --verbose

# Check trigger settings
./rigol-tool.sh --command=query --scpi=":TRIG:EDGE:SOUR?"
./rigol-tool.sh --command=query --scpi=":TRIG:EDGE:LEV?"
```

## Dependencies

Required tools (checked automatically):
- **netcat (nc)** - SCPI communication over TCP
- **jq** - JSON processing
- **rigol_scpi.sh** - Low-level SCPI interface (must be in same directory)

Install on Debian/Ubuntu:
```bash
sudo apt-get install netcat-openbsd jq
```

## Configuration File Management

### Directory Structure
```
dso-scripts/
├── rigol-tool.sh           # Main tool
├── rigol_scpi.sh           # Low-level SCPI backend
├── dso-configs/            # Saved configurations
│   ├── touch-tester/       # Touch latency configs
│   │   ├── latency-2ch.json
│   │   └── verify-single.json
│   └── fpga-project/       # Future: FPGA timing configs
└── RIGOL_TOOL_USAGE.md     # This file
```

### Best Practices
1. **Use descriptive filenames**: `latency-gpio27-gpio7-10ms.json`
2. **Organize by project**: Create subdirectories in `dso-configs/`
3. **Version control**: Commit JSON configs to git for reproducibility
4. **Add comments**: Use `jq` to add description fields

### Editing Configurations Manually

```bash
# View configuration
jq '.' dso-configs/touch-tester/latency-2ch.json

# Change trigger level to 2.0V
jq '.trigger.level = 2.0' latency-2ch.json > temp.json && mv temp.json latency-2ch.json

# Change CH1 scale to 2V/div
jq '.channels."1".scale = 2.0' latency-2ch.json > temp.json && mv temp.json latency-2ch.json
```

## Troubleshooting

### Connection Issues

**Error:** `Cannot connect to oscilloscope at 192.168.1.7:5555`

**Solutions:**
1. Verify oscilloscope IP address:
   ```bash
   ping 192.168.1.7
   ```

2. Check SCPI is enabled on scope:
   - Open web interface: http://192.168.1.7/
   - Go to Settings → I/O Settings
   - Enable SCPI server on port 5555

3. Test with low-level tool:
   ```bash
   ./rigol_scpi.sh -i 192.168.1.7 -q "*IDN?"
   ```

### JSON Parsing Issues

**Error:** `Invalid JSON file`

**Solutions:**
1. Validate JSON syntax:
   ```bash
   jq empty your-config.json
   ```

2. Prettify JSON:
   ```bash
   jq '.' your-config.json > formatted.json
   ```

### Missing Dependencies

**Error:** `jq is required but not installed`

**Solution:**
```bash
sudo apt-get update
sudo apt-get install jq
```

## Next Steps (Future Phases)

Phase 2 will add:
- **measure-delay**: Automated delay measurements between channels
- **measure-stats**: Statistical analysis over multiple triggers
- **capture-screen**: Screenshot capture
- **quick-setup**: Predefined presets for common scenarios

Phase 3 will add:
- **sync-measure**: Synchronize with touch-tester runs
- **validate-latency**: Compare SW vs HW measurements
- **run-batch**: Automated test sequences

## Support

For issues or questions:
1. Check this documentation
2. Use `--verbose` flag for detailed debug output
3. Test connection with `check-connection` command
4. Verify SCPI commands with `query` command
