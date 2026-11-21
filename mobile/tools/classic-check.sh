#!/usr/bin/env bash
# Classic stays as it is: the screen every user has must not change by
# accident when a build adds options (own layouts, new panels...). It may
# change only by a change approved for it and documented (its roadmap item);
# --approve names each difference with that item, and a difference without
# one is a regression to fix, not to approve. This
# compares the working tree's UI with the last release's, size by size,
# against the real radio, and leaves the verdict for the pre-push hook.
#
#   mobile/tools/classic-check.sh [--base REF] [--radio IP] [--sizes WxH,...]
#   mobile/tools/classic-check.sh --approve "what the masks show, and why"
#
# --base: the release to compare with (default: the newest android-beta-N
#   tag, the commit the last release was built from).
# 1. The baseline: the base's mobile/qml on this tree's engine, swept once
#    (render-sweep.py, every window size, DIGU and USB) and kept in
#    ~/.cache/oksasdr-baselines/<base sha>. The working tree's QML is copied
#    aside and put back exactly; git's index is not touched.
# 2. This tree swept the same way.
# 3. layout-diff.py (the page's decisions, the info panel), QML warnings, and
#    a difference mask per size (white = changed; the live spectrum, waterfall
#    and meters always show as noise, anything else is a real change), in a
#    montage to look at.
# 4. The verdict, in two steps so nothing passes unlooked at: a run with no
#    QML warning on a committed tree leaves pending-<key> (key: this
#    commit's mobile/qml and mobile/src trees) with the numbers; after
#    looking at the montage (and layout-diff.txt when a decision changed),
#    --approve "..." says what differs and why it is meant ("only live
#    content; the View tab's new Layout section") and turns it into the
#    stamp ok-<key> the pre-push hook looks for. A dirty tree gets no
#    pending: commit first and run again for the push.
# Receive only (render-sweep): nothing is keyed; it switches this app's slice
# DIGU / USB and back.
set -euo pipefail

root="$(git rev-parse --show-toplevel)"
cd "$root"
radio="192.168.0.40"
sizes=""
approve=""
base=""
while [ $# -gt 0 ]; do
    case "$1" in
        --base) base="$2"; shift 2 ;;
        --radio) radio="$2"; shift 2 ;;
        --sizes) sizes="$2"; shift 2 ;;
        --approve) approve="$2"; shift 2 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
store="$HOME/.cache/oksasdr-baselines"
key="$(git rev-parse HEAD:mobile/qml)-$(git rev-parse HEAD:mobile/src)"
if [ -n "$approve" ]; then
    [ -f "$store/pending-$key" ] || { echo "classic-check: no check of this commit's UI to approve: run it first" >&2; exit 1; }
    git diff --quiet HEAD -- mobile/qml mobile/src || { echo "classic-check: the tree changed since: run again" >&2; exit 1; }
    { cat "$store/pending-$key"; echo "approved $(date -Iseconds): $approve"; } > "$store/ok-$key"
    rm -f "$store/pending-$key"
    echo "classic-check: approved ($store/ok-$key)"
    exit 0
fi
if [ -z "$base" ]; then
    git fetch -q origin 'refs/tags/android-beta-*:refs/tags/android-beta-*' 2>/dev/null || true
    base="$(git tag --list 'android-beta-*' --sort=-v:refname | head -1)"
fi
base_sha="$(git rev-parse --verify "$base^{commit}")"
basedir="$store/${base_sha:0:9}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
sweep_args=(--radio "$radio")
[ -n "$sizes" ] && sweep_args+=(--sizes "$sizes")

build() {
    nice -n 19 ionice -c3 cmake --build build-mobile --target aether-mobile -j6 >"$work/build.log" 2>&1 \
        || { tail -20 "$work/build.log"; echo "classic-check: the build failed" >&2; exit 1; }
}

echo "classic-check: base $base (${base_sha:0:9})"
if [ ! -f "$basedir/done" ] || [ -n "$sizes" ]; then
    # The base's QML in the working tree, then this tree's put back as it was.
    cp -a mobile/qml "$work/qml.saved"
    restore() { rm -rf mobile/qml && cp -a "$work/qml.saved" mobile/qml; }
    trap 'restore; rm -rf "$work"' EXIT
    git ls-tree -r --name-only "$base_sha" -- mobile/qml | while read -r f; do
        mkdir -p "$(dirname "$f")"
        git show "$base_sha:$f" > "$f"
    done
    echo "classic-check: building the base's page"
    build
    [ -n "$sizes" ] && basedir="$work/base"
    rm -rf "$basedir"
    python3 mobile/tools/render-sweep.py "$basedir" "${sweep_args[@]}" | tail -1
    [ -z "$sizes" ] && touch "$basedir/done"
    restore
    trap 'rm -rf "$work"' EXIT
    sleep 20   # the radio's GUI client slot
fi

echo "classic-check: building this tree"
# The build copies the QML into build-mobile by date, not by content: after
# the base's files (written now) were built, this tree's put back with their
# older dates were not copied again, and the "tree" sweep showed the base
# (found 2026-11-21 with build 199's light theme). Every file dated now, so
# the build takes this tree's QML whatever was built before.
command find mobile/qml -type f -exec touch {} +
build
# And proven: every QML file the build holds is this tree's.
for f in mobile/qml/*.qml; do
    copy="build-mobile/mobile/AetherMobile/qml/$(basename "$f")"
    [ -f "$copy" ] && ! cmp -s "$f" "$copy" \
        && { echo "classic-check: the build holds another $(basename "$f") than this tree" >&2; exit 1; }
done
dirty=""
git diff --quiet HEAD -- mobile/qml mobile/src || dirty=1
out="$store/check-${key:0:12}${dirty:+-dirty}"
rm -rf "$out"
summary="$(python3 mobile/tools/render-sweep.py "$out" "${sweep_args[@]}" | tail -1)"
echo "classic-check: $summary"
python3 mobile/tools/layout-diff.py "$basedir" "$out" > "$out/layout-diff.txt" 2>&1 || true
changed="$(command grep -c '!!' "$out/layout-diff.txt" || true)"
moved="$(command grep -cE '^ *~' "$out/layout-diff.txt" || true)"
warnings="$(sed -n 's/.*qml warnings \([0-9]*\).*/\1/p' <<<"$summary")"

# Difference masks and one montage of all sizes.
mkdir -p "$out/diff"
for f in "$basedir"/*-digu.png; do
    n="$(basename "$f")"
    [ -f "$out/$n" ] || continue
    magick "$f" "$out/$n" -compose difference -composite -colorspace gray -threshold 12% "$out/diff/$n"
done
magick montage "$out"/diff/*-digu.png -tile 6x -geometry 300x300+4+4 -background '#600' \
    -label '%t' "$out/diff-montage.jpg"

echo "classic-check: layout decisions changed $changed, panels moved $moved, QML warnings ${warnings:-?}"
echo "classic-check: look at $out/diff-montage.jpg (white outside the live display = a change)"
if [ -n "$dirty" ]; then
    echo "classic-check: uncommitted changes in mobile/qml or mobile/src: nothing to approve (commit, run again)"
    exit 1
fi
if [ "${warnings:-1}" != "0" ]; then
    echo "classic-check: QML warnings: fix them first" >&2
    exit 1
fi
{
    echo "base $base ${base_sha}"
    echo "checked $(date -Iseconds) at HEAD $(git rev-parse HEAD)"
    echo "$summary"
    echo "decisions changed $changed, panels moved $moved"
    echo "the masks: $out/diff-montage.jpg"
} > "$store/pending-$key"
echo "classic-check: look at the montage$([ "$changed" != 0 ] && echo " and $out/layout-diff.txt"), then:"
echo "  mobile/tools/classic-check.sh --approve \"what differs, and why it is meant\""
