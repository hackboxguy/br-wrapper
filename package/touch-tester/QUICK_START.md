# Touch-Tester Quick Start Guide

## Build Instructions

### On Development Machine (WSL2/Debian)
```bash
cd /home/testpc/git-repos/tmp/br-wrapper/package/touch-tester
mkdir build && cd build
cmake -DBUILD_QT_APP=OFF ..
make
```

### On Raspberry Pi 4 (Production)
```bash
# Transfer code first
scp -r touch-tester/ pi@raspberry-pi:/home/pi/

# Then on Pi:
cd touch-tester
mkdir build && cd build
cmake -DBUILD_QT_APP=OFF ..
make

# Setup permissions (one time)
sudo usermod -a -G gpio,input $USER
# Log out and back in
```

## Quick Test Commands

### 1. Help
```bash
./src/touch-tester-cli/touch-tester --help
```

### 2. Single Touch Verification (Verbose)
```bash
./src/touch-tester-cli/touch-tester \
  --testtype=verify \
  --output-gpio=27 \
  --verbose
```

**What it does**: Generates ONE touch pulse, shows detailed event data

### 3. Latency Measurement (10 samples)
```bash
./src/touch-tester-cli/touch-tester \
  --testtype=latencymeasure \
  --output-gpio=27 \
  --loopcount=10 \
  --verbose
```

**What it does**: Measures touch latency 10 times, shows statistics

### 4. Event Count Test (Check for misses)
```bash
./src/touch-tester-cli/touch-tester \
  --testtype=touchcount \
  --output-gpio=27 \
  --loopcount=20
```

**What it does**: Generates 20 pulses, counts how many events received

### 5. Pulse Generation Only
```bash
./src/touch-tester-cli/touch-tester \
  --testtype=touchtrigger \
  --output-gpio=27 \
  --loopcount=5 \
  --pulsewidth-ms=25 \
  --wait-ms=100
```

**What it does**: Just generates GPIO pulses (no measurement)

## Output Formats

### Human-Readable (Default)
```bash
./src/touch-tester-cli/touch-tester --testtype=latencymeasure --output-gpio=27 --loopcount=10
```

### JSON
```bash
./src/touch-tester-cli/touch-tester --testtype=latencymeasure --output-gpio=27 --loopcount=10 --format=json
```

### CSV
```bash
./src/touch-tester-cli/touch-tester --testtype=latencymeasure --output-gpio=27 --loopcount=10 --format=csv > results.csv
```

## Specifying Input Device

### Auto-discover (Default)
```bash
./src/touch-tester-cli/touch-tester --testtype=verify --output-gpio=27
```

### Manual Device
```bash
./src/touch-tester-cli/touch-tester --testtype=verify --output-gpio=27 --inputevent=/dev/input/event6
```

## Common Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--output-gpio` | **Required** | GPIO pin number (e.g., 27) |
| `--inputevent` | `auto` | Touch device path or "auto" |
| `--loopcount` | `1` | Number of test iterations |
| `--pulsewidth-ms` | `25` | Pulse width in milliseconds |
| `--wait-ms` | `50` | Wait between pulses (min: 50ms) |
| `--format` | `human` | Output: human, json, csv |
| `--verbose` | `false` | Enable detailed output |

## Expected Results (Himax @ 120Hz)

- **Min Latency**: ~15 ms
- **Avg Latency**: ~19-20 ms
- **Max Latency**: ~25 ms
- **Missed Events**: 0 (at 50ms wait time)

## Troubleshooting

### GPIO Permission Denied
```bash
sudo usermod -a -G gpio $USER
# Log out and back in
```

### Input Device Permission Denied
```bash
sudo usermod -a -G input $USER
# Log out and back in
```

### Auto-discovery Fails
```bash
# List devices manually
cat /proc/bus/input/devices

# Or use evtest
evtest

# Then use specific device
./src/touch-tester-cli/touch-tester --inputevent=/dev/input/event6 --output-gpio=27 --testtype=verify
```

### No Touch Events Received
1. Verify touch controller works: `evtest /dev/input/event6`
2. Check auto-clicker position on screen
3. Test GPIO manually: `gpioset 0 27=1; sleep 0.025; gpioset 0 27=0`
4. Use `--verbose` flag for debugging

## Installation (Optional)

```bash
sudo make install

# Then run from anywhere:
touch-tester --testtype=verify --output-gpio=27
```

Default install paths:
- Binary: `/usr/bin/touch-tester`
- Library: `/usr/lib/libtouch-test.so`
- Headers: `/usr/include/touch-test/`

## What's Next?

After hardware testing is successful, we'll implement **Phase 2**:
- Qt5 GUI with live visualization
- Network daemon mode (TCP socket API)
- Real-time latency graphs
- Systemd service integration

---

**Build Status**: ✅ Compiles successfully
**Hardware Testing**: ⏳ Pending on Raspberry Pi 4
**Version**: 1.0.0 (Phase 1)
