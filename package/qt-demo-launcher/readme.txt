qt-demo-launcher app network api commands
# Default configuration and port
qt-demo-launcher

# Custom port
qt-demo-launcher --port 8090

# Custom config and port
qt-demo-launcher --config /tmp/my-config.json --port 8082



echo "list-apps" | nc -q 0 192.168.1.92 8081
# Returns: gallery,slideshow,drawing,system,patterns,exit

# Start an app
echo "start-app gallery" | nc -q 0 192.168.1.100 8081
# Returns: OK

# Check it's running  
echo "get-running-app" | nc -q 0 192.168.1.100 8081
# Returns: gallery

# Stop the app
echo "stop-app" | nc -q 0 192.168.1.100 8081  
# Returns: OK

# Verify it stopped
echo "get-running-app" | nc -q 0 192.168.1.100 8081
# Returns: none

# Try stopping when nothing is running
echo "stop-app" | nc -q 0 192.168.1.100 8081
# Returns: ERROR: no-app-running
