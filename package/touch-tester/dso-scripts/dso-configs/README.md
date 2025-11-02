# DSO Configuration Files

This directory stores oscilloscope configuration files in JSON format.

## Directory Structure

```
dso-configs/
├── touch-tester/          # Touch latency testing configurations
├── fpga-project/          # FPGA timing configurations (future)
└── README.md              # This file
```

## Configuration File Format

JSON files created by `rigol-tool.sh --command=copy-setup` contain:

- **version**: Configuration format version
- **timestamp**: When configuration was saved
- **device**: Oscilloscope model and serial number
- **channels**: Channel settings (scale, offset, coupling, etc.)
- **timebase**: Time/div and offset settings
- **trigger**: Trigger source, level, slope, sweep mode

## Usage

### Save Current Configuration
```bash
./rigol-tool.sh --command=copy-setup --output=dso-configs/touch-tester/my-config.json
```

### Restore Configuration
```bash
./rigol-tool.sh --command=apply-setup --input=dso-configs/touch-tester/my-config.json
```

### View Configuration
```bash
jq '.' dso-configs/touch-tester/my-config.json
```

## Best Practices

1. **Use descriptive names**: e.g., `touch-latency-2ch-10ms.json`
2. **Include project subfolder**: Keep configs organized by project
3. **Add comments in filename**: e.g., `config-100samples-persistence.json`
4. **Version control**: Commit these JSON files to git for reproducibility

## Example Configurations

See `touch-tester/` directory for example configurations used in touch latency testing.
