#!/bin/sh
# Choose the als-dimmer configuration for the display this head unit drives,
# then exec the daemon with it.
#
# als-dimmer itself is display-agnostic by design (plan D6.2): it takes a
# --config path and knows nothing about which panel is attached. The head unit
# does know -- config.txt was written for a specific display and
# pi-config-txt.sh reports which -- so the mapping lives here, in the image
# packaging, and is one line per display type.
#
# Why this is ExecStart and not ExecStartPre writing an env file: systemd sets
# up a service's environment, EnvironmentFile included, before ExecStartPre
# runs, so a file written by ExecStartPre is not in scope for ExecStart's
# ${VARIABLE} expansion. The env file is still written, because it is a useful
# thing to look at after the fact, but what actually selects the config is this
# script exec'ing the daemon.

set -e

# The tree is not always at the packaged prefix: the bench images keep both
# als-dimmer and micropanel under /home/pi.
for d in /usr/etc/als-dimmer /etc/als-dimmer /home/pi/als-dimmer/etc/als-dimmer; do
    [ -d "$d" ] && ALS_ETC="$d" && break
done
for b in /usr/bin/als-dimmer /home/pi/als-dimmer/bin/als-dimmer; do
    [ -x "$b" ] && ALS_BIN="$b" && break
done
for q in /usr/bin/pi-config-txt.sh /home/pi/micropanel/usr/bin/pi-config-txt.sh; do
    [ -x "$q" ] && QUERY="$q" && break
done

if [ -z "$ALS_ETC" ] || [ -z "$ALS_BIN" ]; then
    echo "als-dimmer-select-config: cannot locate als-dimmer (bin=$ALS_BIN etc=$ALS_ETC)" >&2
    exit 1
fi

DEFAULT_CONFIG="$ALS_ETC/config.json"
DISPLAY_TYPE=""
if [ -n "$QUERY" ]; then
    DISPLAY_TYPE=$("$QUERY" --input=/boot/firmware/config.txt --query-config 2>/dev/null || true)
fi

# One line per display type. Anything not listed -- including the "unknown" the
# query returns when it cannot tell -- keeps the config it has always used, so
# adding a display here cannot change what an existing one does.
case "$DISPLAY_TYPE" in
    ots-oled-17) CONFIG="$ALS_ETC/config_ioc_opt5001_tcon_ots17.json" ;;
    *)           CONFIG="$DEFAULT_CONFIG" ;;
esac

# A display type we recognise but whose config was not installed is a packaging
# mistake, not a reason to run the wrong config silently.
if [ ! -r "$CONFIG" ]; then
    echo "als-dimmer-select-config: display '$DISPLAY_TYPE' selects $CONFIG, which is missing; falling back to $DEFAULT_CONFIG" >&2
    CONFIG="$DEFAULT_CONFIG"
fi

mkdir -p /run/als-dimmer
echo "ALS_DIMMER_CONFIG=$CONFIG" > /run/als-dimmer/env

echo "als-dimmer-select-config: display '${DISPLAY_TYPE:-unqueried}' -> $CONFIG"
exec "$ALS_BIN" --config "$CONFIG"
