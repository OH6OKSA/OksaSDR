#!/usr/bin/env bash
# Renders the README screenshots (docs/mobile/*.jpg) for every layout size.
#
#   mobile/tools/update-screenshots.sh [RADIO_IP] [--only NAME,...]
#   mobile/tools/update-screenshots.sh --list
#
# --only renders just those (names as the files, without .jpg); names this
# script does not make are left to update-manual-screenshots.sh.
#
# With RADIO_IP the operate pages show that FlexRadio (receive only: nothing
# is keyed or changed on the radio); without it they use the built-in demo.
# The LSB passband shot always uses the demo, so a real radio's mode is
# never touched. Needs a configured AETHER_MOBILE build in build-mobile/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-mobile}"
OUT="$ROOT/docs/mobile"
RADIO=""
ONLY=""
LIST=0
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY="${2:?--only needs NAME,...}"; shift 2 ;;
        --list) LIST=1; shift ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) RADIO="$1"; shift ;;
    esac
done
all_shots=(ipad-mini-portrait iphone-portrait iphone-landscape iphone-landscape-drawer passband-lsb)
if [ "$LIST" = 1 ]; then
    printf '%s\n' "${all_shots[@]}"
    exit 0
fi
# want NAME: true when it is to be rendered
want() {
    [ -z "$ONLY" ] || [[ ",$ONLY," == *",$1,"* ]]
}
any=0
for n in "${all_shots[@]}"; do if want "$n"; then any=1; fi; done
if [ "$any" = 0 ]; then
    echo "No README pictures among: $ONLY"
    exit 0
fi
DELAY="${SCREENSHOT_DELAY_MS:-40000}"
APP="$BUILD/mobile/aether-mobile"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cmake --build "$BUILD" --target aether-mobile --parallel "$(nproc)" >/dev/null
mkdir -p "$OUT"

# The radio takes two GUI clients, and a run's client is freed some seconds
# after it quits: with a phone connected, back-to-back runs found both taken
# and rendered the connect page. A pause after each run on the radio.
settle() {
    if [ -n "$RADIO" ]; then sleep 20; fi
}
shot() { # name size args... (only when wanted)
    local name="$1" size="$2"
    shift 2
    want "$name" || return 0
    QT_QPA_PLATFORM=offscreen timeout 90 "$APP" --size "$size" \
        --screenshot "$TMP/$name.png" --screenshot-delay "$DELAY" "$@" >/dev/null 2>&1 || true
    echo "  $name ($size)"
    settle
}

if [ -n "$RADIO" ]; then
    source_args=(--connect "$RADIO")
else
    source_args=(--demo)
fi

# ipad-landscape.jpg and the android-*.jpg are screenshots from the
# devices (taken over the developer link), so they are not rendered here.
echo "Rendering screenshots (${RADIO:-demo radio})${ONLY:+, only $ONLY}..."
shot ipad-mini-portrait 744x1133 "${source_args[@]}" --controls visible
shot iphone-portrait 440x956 "${source_args[@]}" --controls visible
shot iphone-landscape 956x440 "${source_args[@]}" --controls visible
shot iphone-landscape-drawer 956x440 "${source_args[@]}" --controls drawer
shot passband-lsb 1366x1024 --demo --mode LSB --controls visible

# Strip metadata and compress for the README (the wanted ones; a picture a
# run did not take keeps its old file).
missing=0
jpg() { # name magick-args...
    local name="$1"
    shift
    want "$name" || return 0
    if [ ! -f "$TMP/$name.png" ]; then
        echo "  MISSING: $name" >&2
        missing=1
        return 0
    fi
    magick "$TMP/$name.png" "$@" "$OUT/$name.jpg"
}
jpg ipad-mini-portrait -resize 600x -strip -quality 85
jpg iphone-portrait -strip -quality 88
jpg iphone-landscape -strip -quality 88
jpg iphone-landscape-drawer -strip -quality 88
jpg passband-lsb -crop 990x760+0+0 -resize 700x -strip -quality 85
if [ "$missing" = 1 ]; then
    exit 1
fi
echo "Updated $OUT${ONLY:+ ($ONLY)}"
