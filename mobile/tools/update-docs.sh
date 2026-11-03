#!/usr/bin/env bash
# Refreshes everything GitHub shows of the app, then checks the docs:
#
#   mobile/tools/update-docs.sh RADIO_IP [--only NAME,...] [--sweep NEW_DIR [--baseline OLD_DIR]]
#
# README screenshots and the manual's (against the real radio, receive only:
# nothing is keyed; the manual's RADE picture lends this app's slice TX and
# gives it back), then mobile/tools/docs-check.py, which lists what still
# needs words (manual, TestFlight notes, roadmap).
#
# --only renders just the pictures named (as the files, without .jpg; both
# scripts' --list print them), e.g. --only mode-psk,tab-digi: the runs that
# take them and nothing else.
#
# --sweep runs the layout sweep first (render-sweep.py: every window size,
# into NEW_DIR) and, with --baseline, compares it with the previous build's
# (layout-diff.py), before any screenshot run touches the radio's slices.
# A changed layout decision stops it there: look at both pictures, then
# render the screenshots without --sweep.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLS="$ROOT/mobile/tools"
RADIO=""
ONLY=""
SWEEP=""
BASELINE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY="${2:?--only needs NAME,...}"; shift 2 ;;
        --sweep) SWEEP="${2:?--sweep needs NEW_DIR}"; shift 2 ;;
        --baseline) BASELINE="${2:?--baseline needs OLD_DIR}"; shift 2 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) RADIO="$1"; shift ;;
    esac
done
[ -n "$RADIO" ] || { echo "usage: update-docs.sh RADIO_IP [--only NAME,...] [--sweep NEW_DIR [--baseline OLD_DIR]]" >&2; exit 2; }
if [ -n "$BASELINE" ] && [ -z "$SWEEP" ]; then
    echo "--baseline goes with --sweep" >&2
    exit 2
fi

# A misspelt name would render nothing and look done.
if [ -n "$ONLY" ]; then
    known="$( { "$TOOLS/update-screenshots.sh" --list; "$TOOLS/update-manual-screenshots.sh" --list; } )"
    IFS=',' read -ra names <<<"$ONLY"
    for n in "${names[@]}"; do
        if ! printf '%s\n' "$known" | command grep -qx -- "$n"; then
            echo "no picture named $n (see --list of update-screenshots.sh and update-manual-screenshots.sh)" >&2
            exit 2
        fi
    done
    only=(--only "$ONLY")
else
    only=()
fi

if [ -n "$SWEEP" ]; then
    cmake --build "$ROOT/build-mobile" --target aether-mobile --parallel "$(nproc)" >/dev/null
    echo "Layout sweep into $SWEEP..."
    python3 "$TOOLS/render-sweep.py" "$SWEEP" --radio "$RADIO"
    if [ -n "$BASELINE" ]; then
        if ! python3 "$TOOLS/layout-diff.py" "$BASELINE" "$SWEEP"; then
            echo "Layout decisions changed against $BASELINE (above): look at both pictures, then run without --sweep." >&2
            exit 1
        fi
    fi
    # The sweep's client is freed some seconds after it quits.
    sleep 20
fi

"$TOOLS/update-screenshots.sh" "$RADIO" "${only[@]}"
"$TOOLS/update-manual-screenshots.sh" "$RADIO" "${only[@]}"
python3 "$TOOLS/docs-check.py" || true
