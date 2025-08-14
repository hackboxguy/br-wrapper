A qt based pattern generator app that allows rendering of different patterns or rgb based solid colors(patch) pattern generation can be controlled through touch interactions or through network api command to port 8080 as shown below:

e.g echo "pattern ansi-checker" | nc -q 0 192.168.1.95 8080  (-q 0 is 0sec wait before disconnecting tcp socket connection to the server)

# Basic patterns
pattern grayscale-ramp      # 16-step grayscale gradient
pattern ansi-checker        # Black/white checkerboard

# Solid color patterns  
pattern white               # Full white screen (255,255,255)
pattern black               # Full black screen (0,0,0)
pattern red                 # Full red screen (255,0,0)
pattern green               # Full green screen (0,255,0)
pattern blue                # Full blue screen (0,0,255)
pattern cyan                # Full cyan screen (0,255,255)
pattern magenta             # Full magenta screen (255,0,255)
pattern yellow              # Full yellow screen (255,255,0)

# Advanced patterns
pattern zone-boundary-grid  # 16x9 grid with 144 numbered zones
pattern blooming-detection  # Single pulsing pixel in center
pattern cross-dimming       # 4 corner spots for zone interference

# Custom RGB patches
pattern rgb 255 128 64      # Custom color (R G B values 0-255)
pattern rgb 0 0 0           # Black screen
pattern rgb 255 255 255     # White screen

Information Query Commands
get-resolution              # Returns: 2560x1440
get-pattern                 # Returns current pattern name (e.g., "red")
list-patterns               # Returns: grayscale-ramp,ansi-checker,white-text-black,red,gr



# Check current status (should return "autohide")
echo "get-metadata-status" | nc -q 0 192.168.1.95 8080
# Hide the network info completely
echo "set-metadata-status disable" | nc -q 0 192.168.1.95 8080
# Make it always visible
echo "set-metadata-status enable" | nc -q 0 192.168.1.95 8080
# Return to default auto-hide behavior
echo "set-metadata-status autohide" | nc -q 0 192.168.1.95 8080

# Set single line text
echo "set-metadata-text X:443.666" | nc -q 0 192.168.1.92 8080
# Set multiline text with measurements
echo "set-metadata-text X:443.666311\nY:208.683593\nZ:5.623419" | nc -q 0 192.168.1.92 8080
# Get current metadata text
echo "get-metadata-text" | nc -q 0 192.168.1.92 8080
# Clear back to default IP:port
echo "clear-metadata-text" | nc -q 0 192.168.1.92 8080
# Test with spaces in text
echo "set-metadata-text Temp: 25.3°C\nHumidity: 65%" | nc -q 0 192.168.1.92 8080

set-metadata-align left     # Left-align text
set-metadata-align center   # Center-align text (default)
set-metadata-align right    # Right-align text
get-metadata-align          # Returns current alignment

set-metadata-fontsize 24    # Set font size (8-48 range)
set-metadata-fontsize 12    # Smaller text
get-metadata-fontsize       # Returns current font size

# Named colors
set-metadata-color red
set-metadata-color blue
set-metadata-color yellow

# RGB values  
set-metadata-color 255 128 64
set-metadata-color 0 255 0
get-metadata-color          # Returns RGB values (e.g., "255 128 64")

