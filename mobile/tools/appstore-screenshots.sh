#!/usr/bin/env bash
# App Store screenshots of OksaSDR, rendered against the real radio
# (receive only), in the sizes App Store Connect asks for:
#   6.9" iPhone portrait 1320 x 2868 (440 x 956 points at 3x)
#   13" iPad landscape 2752 x 2064 (1376 x 1032 points at 2x)
#
#   mobile/tools/appstore-screenshots.sh RADIO_IP [--build N] [--freq MHZ --mode MODE] [--only iphone|ipad]
#
# --build N renders with the build number the submitted build has, as the
# status line shows it (the local build otherwise says "(1)"). --freq and
# --mode put slice A where the main screens are taken (7.074 DIGU, whose
# signals fill the spectrum at any hour); slice A's frequency and mode are
# read first and put back at the end. FT8 runs through the app's own FT8
# test on 40 m, which restores what it changed.
#
# The pictures go to mobile/appstore/screenshots/ (JPEG, no alpha). Needs a
# free slice on the radio (the radio takes two GUI clients: 20 s between
# runs) and ImageMagick (magick).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-mobile"
APP="$BUILD/mobile/aether-mobile"
OUT="$ROOT/mobile/appstore/screenshots"
RADIO=""
BUILD_NUMBER=""
FREQ="7.074"
MODE="DIGU"
FILL="${SCREENSHOT_FILL_MS:-40000}"
ONLY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --build) BUILD_NUMBER="${2:?--build needs N}"; shift 2 ;;
        --freq) FREQ="${2:?--freq needs MHZ}"; shift 2 ;;
        --mode) MODE="${2:?--mode needs MODE}"; shift 2 ;;
        --only) ONLY="${2:?--only needs iphone or ipad}"; shift 2 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) RADIO="$1"; shift ;;
    esac
done
[ -n "$RADIO" ] || { echo "usage: appstore-screenshots.sh RADIO_IP [--build N] [--freq MHZ --mode MODE]" >&2; exit 2; }

if [ -n "$BUILD_NUMBER" ]; then
    cmake -S "$ROOT" -B "$BUILD" -DAETHER_MOBILE_BUILD_NUMBER="$BUILD_NUMBER" >/dev/null
fi
nice cmake --build "$BUILD" --target aether-mobile --parallel "$(nproc)" >/dev/null
mkdir -p "$OUT"
TMP="$(mktemp -d)"
echo "  run logs in $TMP (removed when all pictures came)"

# run NAME SIZE SCALE ARGS...: the plan on stdin, one run on the radio.
run() {
    local name="$1" size="$2" scale="$3"
    shift 3
    cat > "$TMP/$name.plan"
    QT_SCALE_FACTOR="$scale" QT_QPA_PLATFORM=offscreen timeout 240 nice "$APP" --size "$size" \
        --connect "$RADIO" --shot-plan "$TMP/$name.plan" --shot-dir "$TMP" "$@" >"$TMP/$name.log" 2>&1 || true
    echo "  $name: $(command grep -c '^shot ' "$TMP/$name.plan") pictures ($size at ${scale}x)"
    sleep 20
}

echo "Rendering App Store screenshots against $RADIO..."
# Slice A as it is, to put back at the end.
printf 'wait 8000\nslices\ndisconnect\n' | run before 440x956 1
read -r A_FREQ A_MODE < <(command grep -m1 -o -E 'shot-plan: slice A [0-9.]+ [A-Z]+' "$TMP/before.log" | awk '{print $4, $5}') || true
if [ -z "${A_FREQ:-}" ]; then
    echo "slice A not found (no free slice on the radio?): see $TMP/before.log" >&2
    exit 1
fi
echo "  slice A was $A_FREQ $A_MODE"

# The iPhone's Log shot types W1AW (ARRL's station: a public lookup) into the
# call field. On the page A runs the ATU and T tunes, so the transmit lock is
# on while it types (the lock holds them as it holds PTT) and off again
# before the picture (locked, PTT reads "SB hold to arm").
LOG_SHOT='drawer TX\nwait 800\ntapname txLockOn\nwait 800\ndrawer Log\nwait 1500\ntapname logCall\nwait 600\nkeys W1AW\nwait 3500\ndrawer TX\nwait 800\ntapname txLockOff\nwait 800\ndrawer Log\nwait 1500\nshot %s\ntapname logCallClear\nwait 800\n'
# Radio Setup on its Transmit section (the Radio section names the radio and
# this computer).
SETUP_SHOT='popup radioSetup\nwait 800\nclick setupTab1\nwait 1500\nshot %s\nclose\n'
# FT8 on the FTx tab with the decode list under the main buttons (List
# only), the scroll put back after; the app's FT8 test tunes 40 m at 8 s,
# decodes, puts the slice back and ends the run. On the iPhone the drawer
# goes full screen for the list; on the iPad the pinned panel is scrolled
# to the list at its end
# ("pin": "drawer" would open the drawer over the pinned panel; the plan puts
# the pinned sections back at its end).
FTX_PHONE='wait 9000\ndrawer Digi\nwait 1000\ntapname digiScrollList\ntapname fullControls\nwait 45000\nshot %s\ntapname fullControls\ntapname digiScrollTab\nwait 800\n'
FTX_PAD='wait 9000\npin Digi\nwait 46000\nscroll 99999\nwait 1200\nshot %s\nscroll 0\nwait 800\n'

# iPhone: the main screen, the Log tab, Big buttons, Radio Setup.
if [ "$ONLY" != ipad ]; then
{
    printf 'wait 8000\nslice A\nmode %s\nfreq %s\nwait %s\n' "$MODE" "$FREQ" "$FILL"
    printf 'shot iphone-1-operate\n'
    printf "$LOG_SHOT" iphone-3-log
    printf 'drawer View\ntapname bigButtonsOpen\nwait 1500\nshot iphone-4-bigbuttons\nholdname bigExit\nwait 1200\n'
    printf "$SETUP_SHOT" iphone-5-setup
    printf 'disconnect\n'
} | run iphone 440x956 3

printf "$FTX_PHONE" iphone-2-ftx | run iphone-ftx 440x956 3 --ft8-test 60,40
fi

if [ "$ONLY" != iphone ]; then
# iPad: the main screen with DSP and Audio pinned beside it, the CW tab,
# Radio Setup.
# (Its Log tab would show this computer's log file path.)
{
    printf 'wait 8000\nslice A\nmode %s\nfreq %s\npin DSP,Audio\nwait %s\n' "$MODE" "$FREQ" "$FILL"
    printf 'shot ipad-1-operate\n'
    printf 'pin CW\nwait 1500\nshot ipad-3-cw\n'
    printf "$SETUP_SHOT" ipad-4-setup
    printf 'disconnect\n'
} | run ipad 1376x1032 2 --controls visible

printf "$FTX_PAD" ipad-2-ftx | run ipad-ftx 1376x1032 2 --controls visible --ft8-test 60,40
fi

# Slice A back where it was, in a run of its own after everything (an
# in-plan restore before the last FT8 run left it on 7.074 DIGU), then checked.
printf 'wait 8000\nslice A\nwait 1500\nmode %s\nwait 1500\nfreq %s\nwait 4000\nslices\ndisconnect\n' "$A_MODE" "$A_FREQ" \
    | run after 440x956 1
echo "  slice A now: $(command grep -o -E 'slice A [0-9.]+ [A-Z]+' "$TMP/after.log" | tail -1 || echo '?') (was $A_FREQ $A_MODE)"
command grep -h 'ft8-test: restored' "$TMP"/*-ftx.log 2>/dev/null | sed 's/^/  /' || true

missing=0
names=()
[ "$ONLY" != ipad ] && names+=(iphone-1-operate iphone-2-ftx iphone-3-log iphone-4-bigbuttons iphone-5-setup)
[ "$ONLY" != iphone ] && names+=(ipad-1-operate ipad-2-ftx ipad-3-cw ipad-4-setup)
for name in "${names[@]}"; do
    if [ -f "$TMP/$name.png" ]; then
        magick "$TMP/$name.png" -alpha off -strip -quality 92 "$OUT/$name.jpg"
        echo "  $name.jpg $(magick identify -format '%wx%h' "$OUT/$name.jpg")"
    else
        echo "  MISSING: $name" >&2
        missing=1
    fi
done
[ "$missing" = 0 ] && rm -rf "$TMP"
if [ -n "$BUILD_NUMBER" ]; then
    cmake -S "$ROOT" -B "$BUILD" -DAETHER_MOBILE_BUILD_NUMBER=1 >/dev/null
    echo "  (the build number is 1 again for the next local build)"
fi
exit "$missing"
