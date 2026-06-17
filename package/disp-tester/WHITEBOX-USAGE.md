# WhiteBox Pattern - Usage Guide

The whitebox pattern supports three sizing modes for flexible colorimeter measurements across different display sizes.

## Command Syntax

```bash
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="MODE"
```

## Sizing Modes

### 1. Percent Mode (Default)
Box size as percentage of smaller screen dimension (1-50%).

**Syntax:**
```bash
--command-arg="whitebox percent VALUE"
--command-arg="whitebox size VALUE"      # Alias for backward compatibility
```

**Examples:**
```bash
# 5% box (very small)
--command-arg="whitebox percent 5"

# 10% box (default)
--command-arg="whitebox"
--command-arg="whitebox percent 10"

# 25% box (larger)
--command-arg="whitebox size 25"
```

**Use case:** Quick visual tests, consistent relative coverage across displays

---

### 2. Pixels Mode
Absolute pixel size (1-2000 pixels square).

**Syntax:**
```bash
--command-arg="whitebox pixels VALUE"
```

**Examples:**
```bash
# 100×100 pixel box
--command-arg="whitebox pixels 100"

# 200×200 pixel box
--command-arg="whitebox pixels 200"

# 500×500 pixel box
--command-arg="whitebox pixels 500"
```

**Use case:** When you know exact pixel requirements for your display

---

### 3. Millimeter Mode
Physical size in millimeters with display diagonal specification.

**Syntax:**
```bash
--command-arg="whitebox mm SIZE diagonal-inch DIAGONAL"
```

**Examples:**
```bash
# 50mm × 50mm box on 15.6" display
--command-arg="whitebox mm 50 diagonal-inch 15.6"

# 30mm × 30mm box on 27" display
--command-arg="whitebox mm 30 diagonal-inch 27"

# 100mm × 100mm box on 35.6" display
--command-arg="whitebox mm 100 diagonal-inch 35.6"
```

**Use case:** Professional colorimeter measurements requiring consistent physical size

---

---

## `whiteboxmm` — Absolute Physical Size (separate pattern)

A dedicated pattern for accurate physical measurements. Unlike `whitebox mm`
(which derives physical size from a diagonal and assumes square pixels), this
takes the **active-area width and height in mm directly** from the panel
datasheet and computes pixels-per-mm independently per axis — so the box is
physically square even on non-square-pixel panels.

**Syntax:**
```bash
--command-arg="whiteboxmm SIZE_MM width-mm WIDTH_MM height-mm HEIGHT_MM"
# optional leading "size" keyword is also accepted:
--command-arg="whiteboxmm size SIZE_MM width-mm WIDTH_MM height-mm HEIGHT_MM"
```

**Example — 50 mm box on a 12.3" 1920×720 panel (active area ≈ 292 × 109.5 mm):**
```bash
./launcher-client --srv=127.0.0.1:8082 --command=pattern \
  --command-arg="whiteboxmm 50 width-mm 292 height-mm 109.5"
```

A dim readout in the top-left corner shows the requested mm, the resulting
pixel dimensions, and the per-axis px/mm — so you can confirm the size without
a ruler.

**Validation limits:** SIZE_MM 1–500, width-mm / height-mm 10–2000, and
SIZE_MM must not exceed either physical dimension. Out-of-range input returns
`ERROR: Invalid parameter` (the legacy `whitebox` command silently ignores bad
values while still replying `OK`).

**Read back the applied values:**
```bash
--command=get-param --command-arg="whiteboxmm size"
--command=get-param --command-arg="whiteboxmm width-mm"
--command=get-param --command-arg="whiteboxmm height-mm"
```

The existing `whitebox` command (percent / pixels / mm modes) is unchanged.

---

## Display Size Reference

Physical box sizes for common configurations:

### 50mm × 50mm Box Examples

| Display Size | Resolution | Command |
|--------------|------------|---------|
| 10.1" | 1920×1200 | `whitebox mm 50 diagonal-inch 10.1` |
| 12.3" | 1920×1080 | `whitebox mm 50 diagonal-inch 12.3` |
| 14.6" | 1920×1080 | `whitebox mm 50 diagonal-inch 14.6` |
| 15.6" | 1920×1080 | `whitebox mm 50 diagonal-inch 15.6` |
| 17.3" | 1920×1080 | `whitebox mm 50 diagonal-inch 17.3` |
| 27.0" | 2560×1440 | `whitebox mm 50 diagonal-inch 27` |
| 35.6" | 3840×2160 | `whitebox mm 50 diagonal-inch 35.6` |

### Calculated Physical Sizes

For 10% box (percent mode) on your displays:

| Display | Resolution | 10% Box Pixels | Approx Physical Size |
|---------|------------|----------------|---------------------|
| 10.1" 16:10 | 1920×1200 | 192×192 | ~42mm × 42mm |
| 12.3" 16:9  | 1920×1080 | 108×108 | ~30mm × 30mm |
| 15.6" 16:9  | 1920×1080 | 108×108 | ~38mm × 38mm |
| 27.0" 16:9  | 2560×1440 | 144×144 | ~65mm × 65mm |
| 35.6" 16:9  | 3840×2160 | 216×216 | ~87mm × 87mm |

---

## Complete Command Examples

### Testing 15.6" Display with Multiple Sizes

```bash
# Start pattern-generator app first
./launcher-client --srv=127.0.0.1:8081 --command=start-app --command-arg=pattern-generator
sleep 2

# Test different sizes
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="whitebox mm 30 diagonal-inch 15.6"
sleep 3
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="whitebox mm 50 diagonal-inch 15.6"
sleep 3
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="whitebox mm 100 diagonal-inch 15.6"
```

### Switching Between Modes

```bash
# Start with percent mode
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="whitebox percent 10"

# Switch to pixels mode
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="whitebox pixels 150"

# Switch to mm mode
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="whitebox mm 50 diagonal-inch 15.6"

# Back to percent mode
./launcher-client --srv=127.0.0.1:8082 --command=pattern --command-arg="whitebox size 15"
```

---

## Technical Details

### Physical Size Calculation

When using `mm` mode with `diagonal-inch`, the system:

1. Calculates aspect ratio from screen resolution
2. Determines physical width/height from diagonal using Pythagorean theorem
3. Converts inches to millimeters (1 inch = 25.4mm)
4. Calculates pixels-per-mm ratio
5. Converts requested mm size to pixels

**Formula:**
```
height_inches = diagonal_inches / sqrt(aspect² + 1)
width_inches = height_inches × aspect
pixels_per_mm = screen_height_pixels / (height_inches × 25.4)
box_size_pixels = box_size_mm × pixels_per_mm
```

### Crosshair Indicator

A dim white crosshair (opacity 0.05) appears when box size < 200 pixels to help with precise positioning for small boxes.

### Validation Limits

- **Percent mode:** 1-50%
- **Pixels mode:** 1-2000 pixels
- **MM mode:** 1-500mm
- **Diagonal:** 5-100 inches

---

## Query Current Settings

```bash
# Get current mode
./launcher-client --srv=127.0.0.1:8082 --command=get-param --command-arg="whitebox mode"

# Get current values
./launcher-client --srv=127.0.0.1:8082 --command=get-param --command-arg="whitebox size"
./launcher-client --srv=127.0.0.1:8082 --command=get-param --command-arg="whitebox pixels"
./launcher-client --srv=127.0.0.1:8082 --command=get-param --command-arg="whitebox mm"
./launcher-client --srv=127.0.0.1:8082 --command=get-param --command-arg="whitebox diagonal-inch"
```

---

## Troubleshooting

**Box appears incorrect size in mm mode:**
- Verify diagonal-inch value matches your actual display size
- Check that display resolution is correctly detected
- Remember: Diagonal is measured corner-to-corner of active display area

**Box too small/large:**
- Percent mode: Adjust percentage (1-50)
- Pixels mode: Calculate desired pixels manually
- MM mode: Verify diagonal-inch parameter

**Need exact colorimeter aperture matching:**
- Measure your colorimeter's aperture diameter (typically 8-12mm)
- Use mm mode with measured diagonal: `whitebox mm 10 diagonal-inch 15.6`
- Fine-tune mm value to match your colorimeter's spot size
