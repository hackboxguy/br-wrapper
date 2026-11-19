#!/bin/sh
# qvue-stack-video.sh - turn a 4500x298 3x QVue video into the "stacked"
# 1500x894 layout: the three panels (x 0-1499, 1500-2999, 3000-4499) one
# above the other. The Pi 4's hardware H.264 decoder stops at 1920 wide, so a
# 4500-wide video can only be software decoded (about 30 fps); the stacked
# file decodes in hardware and dual-video-player warps it at 60 fps (it is
# recognised by its size, 1500x894).
#
#   qvue-stack-video.sh IN.mp4 OUT.mp4 [--crf=N] [--maxrate=RATE] [--preset=P]
#
# Defaults: H.264 High, level 4.2, CRF 18 (visually lossless for most
# content) capped at 25 Mbit/s, preset medium, BT.709 limited range tags (the
# Pi's decoder refuses incomplete colour tags). The frame rate is kept; audio,
# if any, is copied (the player itself plays no audio). Needs ffmpeg/ffprobe
# with libx264 (any PC; on the Pi it works but is slow).
set -eu

usage() {
    echo "usage: $0 IN.mp4 OUT.mp4 [--crf=N] [--maxrate=RATE] [--preset=P]" >&2
    exit 2
}

[ $# -ge 2 ] || usage
in=$1
out=$2
shift 2
crf=18
maxrate=25M
preset=medium
for a in "$@"; do
    case $a in
        --crf=*) crf=${a#--crf=} ;;
        --maxrate=*) maxrate=${a#--maxrate=} ;;
        --preset=*) preset=${a#--preset=} ;;
        *) usage ;;
    esac
done

for t in ffmpeg ffprobe; do
    command -v "$t" >/dev/null 2>&1 || { echo "qvue-stack-video: $t not found" >&2; exit 1; }
done
[ -r "$in" ] || { echo "qvue-stack-video: cannot read $in" >&2; exit 1; }

size=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$in")
if [ "$size" != "4500x298" ]; then
    echo "qvue-stack-video: $in is $size, expected 4500x298" >&2
    exit 1
fi

# audio: copied when there is one
audio="-an"
if [ -n "$(ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 "$in")" ]; then
    audio="-c:a copy"
fi

# shellcheck disable=SC2086 # $audio is two words on purpose
ffmpeg -hide_banner -loglevel warning -stats -y -i "$in" \
    -filter_complex "[0:v]split=3[a][b][c];[a]crop=1500:298:0:0[p1];[b]crop=1500:298:1500:0[p2];[c]crop=1500:298:3000:0[p3];[p1][p2][p3]vstack=inputs=3,format=yuv420p[v]" \
    -map "[v]" -map "0:a?" $audio \
    -c:v libx264 -preset "$preset" -crf "$crf" -maxrate "$maxrate" -bufsize "$maxrate" \
    -profile:v high -level:v 4.2 \
    -x264-params "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=tv" \
    "$out"

echo "qvue-stack-video: wrote $out ($(ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate -of csv=p=0:s=x "$out"))"
