#!/bin/sh
# Host test for the launcher's command port (src/NetworkInterface.cpp):
# builds tests/network-interface-test.cpp against the host's Qt 5 and runs it.
# Run: sh tests/test-network-interface.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../src"
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT

MOC=$(pkg-config --variable=host_bins Qt5Core 2>/dev/null)/moc
[ -x "$MOC" ] || MOC=$(command -v moc-qt5 || command -v moc)
CXXFLAGS="-std=c++17 -fPIC -Wall -Wextra $(pkg-config --cflags Qt5Core)"

cp "$SRC/NetworkInterface.h" "$SRC/NetworkInterface.cpp" "$HERE/network-interface-test.cpp" "$B/"
"$MOC" "$B/NetworkInterface.h" -o "$B/NetworkInterface.moc"
# shellcheck disable=SC2086,SC2046 # word splitting of the flags is wanted
c++ $CXXFLAGS -I"$B" "$B/NetworkInterface.cpp" "$B/network-interface-test.cpp" \
    -o "$B/network-interface-test" $(pkg-config --libs Qt5Core) -lpthread
QT_LOGGING_RULES="*.debug=false" "$B/network-interface-test" "$((20000 + $$ % 20000))"
