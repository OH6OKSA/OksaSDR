#!/usr/bin/env bash
# Renders the manual's screenshots (docs/mobile/manual/*.jpg).
#
#   mobile/tools/update-manual-screenshots.sh [RADIO_IP]
#
# As update-screenshots.sh: with RADIO_IP the pages show that FlexRadio,
# receive only (nothing is keyed); without it the built-in demo. The last
# three shots change this app's own slice on the radio, receive only: CW
# (the paddles), RADE on 14.236 and FT8 decoding, which leaves the slice on
# 14.074 DIGU. Choosers are opened with --popup, drawer tabs with
# --controls-tab. Needs a configured AETHER_MOBILE build in build-mobile/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-mobile}"
OUT="$ROOT/docs/mobile/manual"
RADIO="${1:-}"
DELAY="${SCREENSHOT_DELAY_MS:-40000}"
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

shot() { # name size args...
    local name="$1" size="$2"
    shift 2
    QT_QPA_PLATFORM=offscreen timeout 90 "$APP" --size "$size" \
        --screenshot "$TMP/$name.png" --screenshot-delay "$DELAY" "$@" >/dev/null 2>&1
    echo "  $name ($size)"
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
shot connect 1366x1024
shot overview 1366x1024 "${src[@]}" --controls visible
shot phone-portrait 440x956 "${src[@]}" --controls visible
shot phone-landscape 956x440 "${src[@]}" --controls visible
shot filter 1366x1024 "${src[@]}" --controls visible --popup filterChooser
shot agc 1366x1024 "${src[@]}" --controls visible --popup agcChooser
shot antenna 1366x1024 "${src[@]}" --controls visible --popup rxAntennaChooser
shot mode 1366x1024 "${src[@]}" --controls visible --popup modeChooser
# Two bars at the left, analog at the right, as while transmitting; the
# meter chooser after it with both analog (styles are remembered).
shot tx-meters 1366x1024 "${src[@]}" --controls visible --preview-tx --meter-style two,analog
shot meter 1366x1024 "${src[@]}" --controls visible --meter-style analog,analog --popup meterChooser
shot frequency 1366x1024 "${src[@]}" --controls visible --frequency-dialog
shot step 1366x1024 "${src[@]}" --controls visible --popup stepChooser
shot volume 1366x1024 "${src[@]}" --controls visible --popup volumePopup
shot split 1366x1024 "${src[@]}" --controls visible --popup splitChooser
shot ipad-portrait 1024x1366 "${src[@]}" --controls visible
shot phone-menu 440x956 "${src[@]}" --tap-at 0.93,0.03,9000
shot bandchooser 844x390 "${src[@]}" --popup bandChooser
shot noslice 1366x1024 "${src[@]}" --controls visible --preview-no-slice
shot network 1366x1024 "${src[@]}" --controls visible --popup networkDialog
shot setup 1366x1024 "${src[@]}" --controls visible --popup radioSetup
shot tnf 1366x1024 "${src[@]}" --controls visible --popup tnfPopup
shot memory-edit 440x956 "${src[@]}" --controls drawer --controls-tab 6 --popup memoryEditDialog
shot tx-processing 440x956 "${src[@]}" --controls drawer --controls-tab 2 --controls-scroll 580
tabs=(dsp audio tx cw view profiles memory log digi freedv)
for i in "${!tabs[@]}"; do
    shot "tab-${tabs[$i]}" 440x956 "${src[@]}" --controls drawer --controls-tab "$i"
done
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
jpg tx-processing 440x520+0+436
for t in "${tabs[@]}"; do
    jpg "tab-$t" 440x520+0+436
done
echo "Updated $OUT"
