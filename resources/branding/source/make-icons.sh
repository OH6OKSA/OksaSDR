#!/bin/sh
# The app icons made from the vector master oksasdr-symbol.svg (roadmap 57):
# iOS (the iPhone, iPad and Mac set), Android's adaptive foreground, legacy
# and round launcher icons at five densities, and the Play listing's icon.
# The mark keeps the size the raster set had, measured from its files:
#   iOS            79.6 % of the icon, on white, RGB without alpha
#   Android fg     248 of 432 px (57 % of the 108 dp canvas, inside the
#                  66 dp safe zone), transparent; the white background
#                  layer is not touched
#   legacy, round  70.8 % on a white square / a white circle
#   Play 512       71.5 % on white, RGB without alpha
# The SVG's own frame is tight (the mark takes about 93 % of it), so the mark
# is trimmed first and placed by these sizes. Needs rsvg-convert and
# ImageMagick 7 (magick).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
ios="$root/mobile/ios/Assets.xcassets/AppIcon.appiconset"
res="$root/mobile/android/res"
play="$root/resources/branding/android/play-store"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The mark alone, large enough that every size is a downscale.
rsvg-convert -w 4096 -h 4096 "$here/oksasdr-symbol.svg" -o "$tmp/full.png"
magick "$tmp/full.png" -trim +repage "$tmp/mark.png"

# mark CANVAS PERCENT: the mark's width in pixels at that canvas.
mark() {
    awk -v c="$1" -v p="$2" 'BEGIN { printf "%d", c * p / 100 + 0.5 }'
}

# opaque CANVAS PERCENT OUT: the mark on white, RGB, no alpha.
opaque() {
    m=$(mark "$1" "$2")
    magick -size "$1x$1" xc:white \( "$tmp/mark.png" -resize "${m}x${m}" \) \
        -gravity center -composite -alpha off -define png:color-type=2 "$3"
}

# foreground CANVAS PERCENT OUT: the mark on transparent.
foreground() {
    m=$(mark "$1" "$2")
    magick -size "$1x$1" xc:none \( "$tmp/mark.png" -resize "${m}x${m}" \) \
        -gravity center -composite -define png:color-type=6 "$3"
}

# round CANVAS PERCENT OUT: the mark on a white circle, transparent outside
# (the circle drawn at four times the size for a smooth edge).
round() {
    m=$(mark "$1" "$2")
    big=$(($1 * 4))
    magick -size "${big}x${big}" xc:none -fill white \
        -draw "circle $((big / 2)),$((big / 2)) $((big / 2)),0" -resize "$1x$1" \
        \( "$tmp/mark.png" -resize "${m}x${m}" \) \
        -gravity center -composite -define png:color-type=6 "$3"
}

# iOS: the App Store's 1024 and the twelve home screen, Spotlight and
# Settings sizes.
opaque 1024 79.6 "$ios/AppIcon-1024.png"
for size in 20 29 40 58 60 76 80 87 120 152 167 180; do
    opaque "$size" 79.6 "$ios/Icon-$size.png"
done

# Android: mdpi 1x ... xxxhdpi 4x of the 108 dp adaptive canvas and the
# 48 dp legacy icons.
for pair in mdpi:1 hdpi:1.5 xhdpi:2 xxhdpi:3 xxxhdpi:4; do
    density=${pair%%:*}
    scale=${pair#*:}
    fg=$(awk -v s="$scale" 'BEGIN { printf "%d", 108 * s }')
    legacy=$(awk -v s="$scale" 'BEGIN { printf "%d", 48 * s }')
    foreground "$fg" 57.4 "$res/mipmap-$density/ic_launcher_foreground.png"
    opaque "$legacy" 70.8 "$res/mipmap-$density/ic_launcher.png"
    round "$legacy" 70.8 "$res/mipmap-$density/ic_launcher_round.png"
done

opaque 512 71.5 "$play/play_store_512.png"

echo "icons made from $(basename "$here")/oksasdr-symbol.svg"
