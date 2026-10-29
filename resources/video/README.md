# Video display metadata fixtures

The first three files were generated from a synthetic red 16×8 frame and silent
mono audio; the fourth uses a synthetic red 16×16 frame without audio. They
contain no external media. They are immutable test inputs,
not build outputs. The original generation used FFmpeg 9.0.2:

```sh
ffmpeg -nostdin -n -f lavfi -i 'color=c=red:s=16x8:r=1:d=1' \
  -f lavfi -i 'anullsrc=r=48000:cl=mono:d=1' -vf 'setsar=2/1' \
  -c:v mpeg4 -q:v 2 -threads 1 -c:a aac -shortest -movflags +faststart \
  resources/video/display-sar-2x.mp4
ffmpeg -nostdin -n -display_rotation:v:0 90 -i resources/video/display-sar-2x.mp4 \
  -map 0 -c copy -movflags +faststart resources/video/display-matrix-90.mp4
ffmpeg -nostdin -n -i resources/video/display-sar-2x.mp4 -map 0 -c copy \
  -metadata:s:v:0 rotate=90 resources/video/display-tagged.mkv
ffmpeg -nostdin -n -f lavfi -i 'color=c=red:s=16x16:r=1:d=1' \
  -c:v libx264 -preset ultrafast -tune zerolatency -threads 1 -pix_fmt yuv420p \
  -bsf:v 'h264_metadata=display_orientation=insert:rotate=90' -an -f matroska \
  resources/video/display-frame-sei-90.mkv
```

Independent `ffprobe` inspection reports sample aspect ratio 2:1, MPEG-4
video and one AAC stream in all three. The second file has a 90-degree display
matrix; the third has a `ROTATE=90` tag without a display matrix. The fourth
has a display-orientation SEI on its decoded frame and no stream-level
display matrix, as confirmed independently with `ffprobe -show_frames` and
`ffprobe -show_streams`.

Encoded byte identity may differ with other FFmpeg versions. SHA-256 of these
stored inputs:

- `display-sar-2x.mp4`: `2cc148f860085fb807205011d3fa71306a90c43951bf510320838b49cd00ad19`
- `display-matrix-90.mp4`: `5a7071895487b6e4b97488cf26d60c49c5fa343264064d721074b202d8a84e33`
- `display-tagged.mkv`: `7caeeaa0812b112dca41b5ed6826511449d7e596a820f40a4749855077199337`
- `display-frame-sei-90.mkv`: `f29fe1fbf1bf8810713995614c09f4a1d4fc85c5c4fc3522237cd282022d1f17`
