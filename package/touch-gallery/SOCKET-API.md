# Touch Gallery Socket API Documentation

## Overview

The touch-gallery application now includes a TCP socket server (default port: 8086) for remote control and automation.

## Command Line Options

```bash
touch-gallery [OPTIONS] [directory]

Options:
  -h, --help              Show help message
  --version               Show version information
  -p, --port <port>       TCP server port (default: 8086)
  -s, --slideshow <sec>   Enable slideshow mode with interval in seconds
  --enable-usb-copy       Show COPY USB button for the current image
  --usb-copy-script <path>
                          Script used by COPY USB button

Positional Arguments:
  directory               Pictures directory to display (default: /Pictures)
```

## Example Usage

```bash
# Start with default settings (port 8086, /Pictures directory)
touch-gallery

# Start with custom directory
touch-gallery /media/images

# Start with custom port
touch-gallery --port 8084 /media/images

# Start in slideshow mode (5 second interval)
touch-gallery --slideshow 5 /media/images

# Start report gallery with USB copy enabled
touch-gallery --enable-usb-copy /home/pi/test-reports
```

## Socket API Commands

All commands are text-based with newline delimiters. Responses end with `\n`.

### Core Commands

#### `list-images [directory]`
**Description**: Returns comma-separated list of image filenames
**Arguments**:
- `directory` (optional): Directory to scan for images
**Response**:
- Success: `image1.jpg,image2.png,image3.jpg\n`
- Empty: `\n` (no images found)
**Example**:
```bash
echo "list-images" | nc 127.0.0.1 8086
# Returns: photo1.jpg,photo2.png,landscape.jpg
```

#### `display <filepath>`
**Description**: Displays specific image file
**Arguments**:
- `filepath`: Full path or filename (relative to current directory)
**Response**:
- Success: `OK\n`
- Error: `ERROR: Image not found\n`
**Example**:
```bash
echo "display photo1.jpg" | nc 127.0.0.1 8086
# Returns: OK
```

#### `get-image`
**Description**: Returns currently displayed image filename
**Response**:
- Success: `filename.jpg\n`
- Empty: `\n` (no images loaded)
**Example**:
```bash
echo "get-image" | nc 127.0.0.1 8086
# Returns: photo1.jpg
```

#### `next`
**Description**: Navigate to next image
**Response**: `OK\n`
**Example**:
```bash
echo "next" | nc 127.0.0.1 8086
# Returns: OK
```

#### `prev`
**Description**: Navigate to previous image
**Response**: `OK\n`
**Example**:
```bash
echo "prev" | nc 127.0.0.1 8086
# Returns: OK
```

### State Query Commands

#### `get-count`
**Description**: Returns total image count
**Response**: `<number>\n`
**Example**:
```bash
echo "get-count" | nc 127.0.0.1 8086
# Returns: 42
```

#### `get-index`
**Description**: Returns current image index (0-based)
**Response**: `<number>\n`
**Example**:
```bash
echo "get-index" | nc 127.0.0.1 8086
# Returns: 5
```

#### `get-directory`
**Description**: Returns current pictures directory
**Response**: `<path>\n`
**Example**:
```bash
echo "get-directory" | nc 127.0.0.1 8086
# Returns: /media/images
```

### Configuration Commands

#### `set-directory <directory>`
**Description**: Changes pictures directory and rescans images
**Arguments**:
- `directory`: New directory path (supports spaces)
**Response**: `OK\n`
**Example**:
```bash
echo "set-directory /media/photos" | nc 127.0.0.1 8086
# Returns: OK
```

#### `copy-current-to-usb`
**Description**: Starts copying the currently displayed image to USB.
**Response**:
- Success: `OK: Copy started\n`
- Error: `ERROR: <reason>\n`
**Example**:
```bash
echo "copy-current-to-usb" | nc 127.0.0.1 8086
# Returns: OK: Copy started
```

#### `get-usb-copy-status`
**Description**: Returns the latest USB copy status text.
**Response**:
- Status text, for example `Copied to USB. Safe to remove.\n`

#### `quit`
**Description**: Closes the application
**Response**: `OK\n`
**Example**:
```bash
echo "quit" | nc 127.0.0.1 8086
# Returns: OK
```

## Using launcher-client

The `launcher-client` utility can be used for remote control:

```bash
# List images
./launcher-client --srv=192.168.1.197:8086 --command=list-images

# Display specific image
./launcher-client --srv=192.168.1.197:8086 --command=display --command-arg=photo1.jpg

# Get current image
./launcher-client --srv=192.168.1.197:8086 --command=get-image

# Navigate
./launcher-client --srv=192.168.1.197:8086 --command=next
./launcher-client --srv=192.168.1.197:8086 --command=prev

# Get state
./launcher-client --srv=192.168.1.197:8086 --command=get-count
./launcher-client --srv=192.168.1.197:8086 --command=get-index

# Copy current image to USB
./launcher-client --srv=192.168.1.197:8086 --command=copy-current-to-usb
```

## Error Responses

- `ERROR: Empty command\n` - No command provided
- `ERROR: Image not found\n` - Specified image doesn't exist
- `ERROR: Unknown command\n` - Command not recognized

## Integration with micropanel Scripts

### Example: list-images.sh
```bash
#!/bin/bash
# Query touch-gallery for image list
echo "list-images $IMAGES_DIR" | nc 127.0.0.1 8086 | tr ',' '\n'
```

### Example: play-image.sh
```bash
#!/bin/bash
IMAGE_FILE="$1"
echo "display $IMAGE_FILE" | nc 127.0.0.1 8086
```

### Example: get-current-image.sh
```bash
#!/bin/bash
echo "get-image" | nc 127.0.0.1 8086
```

## Protocol Details

- **Transport**: TCP
- **Port**: 8086 (configurable via `-p` option)
- **Encoding**: UTF-8
- **Line Delimiter**: `\n` (newline)
- **Command Format**: `<command> [arg1] [arg2] ...\n`
- **Response Format**: `<response>\n`
- **Connection**: Supports single client at a time
- **Reconnection**: Automatically accepts new connections when client disconnects

## Architecture

The socket API follows the same pattern as other Qt applications in the system:

1. **NetworkInterface**: Low-level TCP socket handling (non-blocking I/O with QSocketNotifier)
2. **GalleryController**: Command parser and handler with Qt signal/slot connections
3. **QML Integration**: Bidirectional state synchronization between C++ and QML

## Building

```bash
cd br-wrapper/package/touch-gallery
mkdir build && cd build
cmake ..
make
```

The built binary will be at `build/touch-gallery`.
