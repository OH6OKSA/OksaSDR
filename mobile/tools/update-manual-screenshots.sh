#!/usr/bin/env bash
# Renders the manual's screenshots (docs/mobile/manual/*.jpg).
#
#   mobile/tools/update-manual-screenshots.sh [RADIO_IP]
#
# As update-screenshots.sh: with RADIO_IP the pages show that FlexRadio,
# receive only (nothing is keyed); without it the built-in demo. Most
# pictures come from one run per window size (--shot-plan, ShotPlan.h:
# connect once, let the display fill, then open each chooser, take it and
# close it), a few minutes in all instead of 45 s a picture. The last three
# runs change this app's own slice on the radio, receive only: CW (the
# paddles), RADE on 14.236 and FT8 decoding, which leaves the slice on
# 14.074 DIGU. Needs a configured AETHER_MOBILE build in build-mobile/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-mobile}"
OUT="$ROOT/docs/mobile/manual"
RADIO="${1:-}"
DELAY="${SCREENSHOT_DELAY_MS:-40000}"
# How long a plan lets the display fill before its first picture.
FILL="${SCREENSHOT_FILL_MS:-30000}"
APP="$BUILD/mobile/aether-mobile"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cmake --build "$BUILD" --target aether-mobile --parallel >/dev/null
mkdir -p "$OUT"

if [ -n "$RADIO" ]; then
    src=(--connect "$RADIO")
else
    src=(--demo)
fi

# The radio takes two GUI clients, and a run's client is freed some seconds
# after it quits: with a phone connected, back-to-back runs found both taken
# and rendered the connect page. A pause after each run on the radio.
settle() {
    if [ -n "$RADIO" ]; then sleep 20; fi
}
shot() { # name size args...: one picture, one run
    local name="$1" size="$2"
    shift 2
    QT_QPA_PLATFORM=offscreen timeout 90 "$APP" --size "$size" \
        --screenshot "$TMP/$name.png" --screenshot-delay "$DELAY" "$@" >/dev/null 2>&1
    echo "  $name ($size)"
    settle
}
plan() { # name size args...: the plan on stdin, many pictures in one run
    local name="$1" size="$2"
    shift 2
    cat > "$TMP/$name.plan"
    QT_QPA_PLATFORM=offscreen timeout 300 "$APP" --size "$size" \
        --shot-plan "$TMP/$name.plan" --shot-dir "$TMP" "$@" >/dev/null 2>&1
    echo "  $name: $(grep -c '^shot ' "$TMP/$name.plan") pictures ($size)"
    settle
}
# jpg NAME [CROP] [RESIZE]: write docs/mobile/manual/NAME.jpg
jpg() {
    local name="$1" crop="${2:-}" resize="${3:-}"
    local args=()
    [ -n "$crop" ] && args+=(-crop "$crop" +repage)
    [ -n "$resize" ] && args+=(-resize "$resize")
    magick "$TMP/$name.png" "${args[@]}" -strip -quality 85 "$OUT/$name.jpg"
}

echo "Rendering manual screenshots (${RADIO:-demo radio})..."
printf 'wait 3000\nshot connect\n' | plan connect 1366x1024

plan ipad 1366x1024 "${src[@]}" --controls visible <<PLAN
wait $FILL
shot overview
popup filterChooser
shot filter
close
popup agcChooser
shot agc
close
popup rxAntennaChooser
shot antenna
close
popup modeChooser
shot mode
close
frequency-dialog
shot frequency
close
popup stepChooser
shot step
close
popup volumePopup
shot volume
close
popup splitChooser
shot split
close
popup networkDialog
shot network
close
popup radioSetup
shot setup
close
popup tnfPopup
shot tnf
close
no-slice on
shot noslice
no-slice off
# Two bars at the left, analog at the right, as while transmitting; the
# meter chooser after it with both analog (styles are remembered).
meter-style two,analog
preview-tx on
shot tx-meters
preview-tx off
meter-style analog,analog
popup meterChooser
shot meter
close
PLAN

printf 'wait %s\nshot ipad-portrait\n' "$FILL" | plan ipad-portrait 1024x1366 "${src[@]}" --controls visible

tabs=(dsp audio tx essb cw view profiles memory log digi freedv rtty)
{
    echo "wait $FILL"
    echo "shot phone-portrait"
    echo "tap 0.93,0.03"
    echo "shot phone-menu"
    echo "close"
    echo "drawer 7"
    echo "popup memoryEditDialog"
    echo "shot memory-edit"
    echo "close"
    for i in "${!tabs[@]}"; do
        echo "drawer $i"
        echo "shot tab-${tabs[$i]}"
    done
} | plan phone 440x956 "${src[@]}"

printf 'wait %s\nshot phone-landscape\n' "$FILL" | plan phone-landscape 956x440 "${src[@]}" --controls visible
printf 'wait %s\npopup bandChooser\nshot bandchooser\n' "$FILL" | plan bandchooser 844x390 "${src[@]}"

# Each mode on a phone (Operating by mode): this app's active slice tuned
# to a frequency for the mode, receive only, the waterfall given time to
# fill; the TX tab's FM repeater section and Lock row; RTTY with a test
# text through the decoder, its panel and the Digi tab with RTTY on (the
# top, then the calls heard and macros: the phone run's tab-rtty is
# replaced); PSK31 on 14.0695 with a test signal 6 Hz off the carrier.
# Back on 14.074 DIGU after.
plan modes 440x956 "${src[@]}" <<PLAN
ready
wait 20000
freq 14.200
mode USB
wait 20000
shot mode-usb
freq 14.030
mode CW
wait 20000
shot mode-cw
freq 7.150
mode AM
wait 20000
shot mode-am
freq 29.600
mode FM
wait 20000
shot mode-fm
drawer 2
wait 1500
scroll 0
wait 800
shot fm-tx
scroll 550
wait 800
shot tx-lock
close
freq 14.085
mode RTTY
wait 15000
rttyfeed CQ CQ DE OH2XYZ OH2XYZ OH2XYZ PSE K
wait 8000
shot mode-rtty
popup rttyPanel
wait 1500
shot rtty-panel
close
drawer 11
wait 1500
scroll 0
wait 800
shot tab-rtty
scroll 200
wait 800
shot digi-operating
close
textmode PSK31
wait 2000
freq 14.0695
wait 15000
textclear
pskfeed +6 CQ CQ CQ de OH2XYZ OH2XYZ OH2XYZ pse k
wait 14000
shot mode-psk
textmode RTTY
wait 2000
freq 14.074
mode DIGU
wait 3000
PLAN

# Situations (they change this app's slice; FT8 last, back on 14.074 DIGU).
shot cw-paddles 440x956 "${src[@]}" --set-mode CW --cw-paddles on
shot rade-on 440x956 "${src[@]}" --rade-test 60,14.236
shot ft8-running 440x956 "${src[@]}" --ft8-test 120

jpg connect "" 900x
jpg overview "" 1100x
jpg phone-portrait
jpg phone-landscape
cp "$TMP/overview.png" "$TMP/infopanel.png"
cp "$TMP/overview.png" "$TMP/slicerow.png"
cp "$TMP/overview.png" "$TMP/spectrum.png"
jpg infopanel 500x95+485+45
jpg slicerow 985x140+0+205
jpg spectrum 985x600+0+340 800x
jpg filter 620x420+470+40
jpg agc 560x320+470+40
jpg antenna 620x270+470+40
jpg mode 520x230+320+40
jpg meter 380x600+0+200
jpg step 340x260+505+50
jpg volume 390x350+500+130
jpg split 520x240+470+40
jpg ipad-portrait "" 600x
jpg phone-menu 440x230+0+0
jpg bandchooser "" 700x
jpg cw-paddles 440x200+0+756
jpg rade-on 440x620+0+0
jpg ft8-running "" 330x
jpg tx-meters 985x140+0+200
jpg frequency 700x720+333+150 500x
jpg noslice 985x600+0+340 800x
jpg network 460x600+448+212
jpg setup 640x600+363+50
jpg tnf 967x492+13+354 800x
jpg memory-edit 440x380+0+280
for t in "${tabs[@]}"; do
    jpg "tab-$t" 440x520+0+436
done
for m in usb cw am fm rtty psk; do
    jpg "mode-$m" "" 330x
done
jpg rtty-panel 440x440+0+172
jpg digi-operating 440x332+0+536
jpg fm-tx 440x160+0+532
jpg tx-lock 440x110+0+596
echo "Updated $OUT"
