#!/usr/bin/env bash
# Regenerates the audiobook fixtures: three one-second tone chapters (440, 880 and 1320 Hz)
# with Nero and QuickTime chapter lists and iTunes title/author tags. Requires ffmpeg.
set -euo pipefail
cd "$(dirname "$0")"
ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "sine=frequency=440:duration=1:sample_rate=22050" \
  -f lavfi -i "sine=frequency=880:duration=1:sample_rate=22050" \
  -f lavfi -i "sine=frequency=1320:duration=1:sample_rate=22050" \
  -i chapters.txt -filter_complex "[0][1][2]concat=n=3:v=0:a=1[a]" -map "[a]" \
  -map_metadata 3 -map_chapters 3 -c:a aac -b:a 32k -ac 1 -ar 22050 -f ipod tones-mono-22k.m4b
# Same stream with the movie header before the media data.
ffmpeg -hide_banner -loglevel error -y -i tones-mono-22k.m4b -map 0 -c copy -movflags +faststart -f mp4 \
  tones-mono-22k-faststart.m4b
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=440:duration=3:sample_rate=44100" \
  -i chapters.txt -map 0:a -map_metadata 1 -map_chapters 1 -c:a aac -b:a 64k -ac 2 -ar 44100 -f ipod \
  tones-stereo-44k.m4b
