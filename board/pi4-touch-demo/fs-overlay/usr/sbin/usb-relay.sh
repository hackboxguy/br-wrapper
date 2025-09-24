#! /bin/sh
# USB relay module: LCUS-1 (http://images.100y.com.tw/pdf_file/57-LCUS-1.pdf)
# Test Command:
#     $ /bin/sh -c "echo -n -e '\xA0\x01\x01\xA2' > /dev/ttyUSB1" # COM-ON
#     $ /bin/sh -c "echo -n -e '\xA0\x01\x00\xA1' > /dev/ttyUSB1" # COM-OFF

# Fixed hex codes - ON/OFF were swapped in original
HEX_CODE_OFF='\xA0\x01\x00\xA1'
HEX_CODE_ON='\xA0\x01\x01\xA2'

serdev="$1"
op="$2"
argv="$#"

usage() {
        cat <<EOF
Usage: $0 <path_to_tty_device> <0|1>
       0: Turn the Relay OFF
       1: Turn the Relay ON
EX: $0 /dev/ttyUSB1 0

Note: Make sure your user is in the 'dialout' group:
      sudo usermod -a -G dialout \$USER
      Then log out and back in, or run: newgrp dialout
EOF
}

do_init() {
        if [ "$argv" != "2" ] || [ "$op" = "" ]; then
                usage
                exit 1
        fi
        
        # Root permission check removed - using dialout group permissions instead
        
        if [ ! -c "$serdev"  ]; then
                cat << EOF
Error: There is no device node: ${serdev}
       Make sure USB-to-TTL driver is loaded! (ex: usbserial,pl2303)
       
       If you get permission denied, ensure your user is in dialout group:
       sudo usermod -a -G dialout \$USER
       Then log out and back in.
EOF
                exit 1
        fi
}

hex2ser() {
        local action="$1"
        
        # Set serial port parameters for LCUS-1 module (9600 baud, 8N1, raw mode)
        stty -F "$serdev" 9600 cs8 -cstopb -parenb raw 2>/dev/null
        
        if [ "$action" = "ON" ]; then
                /bin/echo -n -e "$HEX_CODE_ON" > "$serdev"
        else
                /bin/echo -n -e "$HEX_CODE_OFF" > "$serdev"
        fi
}

ser2relay() {
        case "$op" in
        1|[Oo][Nn])
                hex2ser "ON"
                ;;
        0|[Oo][Ff][Ff])
                hex2ser "OFF"
                ;;
        *)
                usage
                exit 1
                ;;
        esac
}

do_main() {
        do_init && \
        ser2relay
}

do_main
