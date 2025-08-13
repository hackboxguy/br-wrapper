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

