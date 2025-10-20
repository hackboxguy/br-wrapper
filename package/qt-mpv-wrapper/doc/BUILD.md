# Building qt-mpv-wrapper

## Prerequisites

```bash
# Install Qt development packages
sudo apt install -y \
    qtbase5-dev \
    qtdeclarative5-dev \
    qml-module-qtquick2 \
    qml-module-qtquick-controls2 \
    qml-module-qtquick-layouts \
    qt5-qmake \
    build-essential
```

## Build Steps

```bash
cd ~/git-repos/br-wrapper/package/qt-mpv-wrapper/src

# Generate Makefile
qmake qt-mpv-wrapper.pro

# Compile
make

# Result
ls -lh qt-mpv-wrapper
```

## Testing Locally

```bash
# Create test video directory
mkdir -p ~/test-videos
# Copy some test videos there

# Run wrapper
export QT_QPA_PLATFORM=linuxfb
export QT_QPA_FONTDIR=/usr/share/fonts/dejavu/
export XDG_RUNTIME_DIR=/tmp/runtime-root
mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR

./qt-mpv-wrapper ~/test-videos
```

## Deployment to Pi4

```bash
# Copy binary
scp qt-mpv-wrapper pi@PI_IP:~/micropanel/qt-apps/

# On Pi4
cd ~/micropanel/qt-apps
chmod +x qt-mpv-wrapper

# Create Videos directory
mkdir -p Videos

# Copy some test videos
# scp *.mp4 pi@PI_IP:~/micropanel/qt-apps/Videos/

# Install mpv
sudo apt install -y mpv

# Test
./qt-mpv-wrapper
```

## Add to qt-demo-launcher

Edit `qt-demo-launcher.json`:

```json
{
  "id": "video-player",
  "enabled": true,
  "text": "Video Player",
  "icon": "/usr/share/icons/video-large.png",
  "program": "/home/pi/micropanel/qt-apps/qt-mpv-wrapper",
  "arguments": ["/home/pi/micropanel/qt-apps/Videos"],
  "working_directory": "/home/pi/micropanel/qt-apps",
  "position": {
    "row": 2,
    "column": 0
  },
  "size": {
    "width": 640,
    "height": 200
  },
  "icon_size": {
    "width": 100,
    "height": 100
  },
  "icon_layout": "icon_top",
  "font_size": 30,
  "background_color": "#9B59B6",
  "hover_color": "#8E44AD",
  "border_radius": 20
}
```

Then restart the launcher:

```bash
sudo systemctl restart qt-demo-launcher.service
```

## Clean Build

```bash
make clean
rm -f Makefile .qmake.stash
qmake && make
```
