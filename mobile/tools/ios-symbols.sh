#!/usr/bin/env bash
# The symbols of exactly the binary in an iOS archive, for tester crash
# reports (.ips) and App Store Connect's own symbolication.
#
#   mobile/tools/ios-symbols.sh ARCHIVE BUILD_DIR OUT_DIR
#
# Xcode links the app straight into the archive's products and strips it
# there (strip -D), so no unstripped copy of it is left anywhere. Its dSYM is
# made from the linked binary before the strip, but the CMake-generated
# project puts it beside the build products (BUILD_DIR/mobile/
# Release-iphoneos/aether-mobile.app.dSYM), not in the archive: the archive's
# dSYMs folder stayed empty and the export said "Upload Symbols Failed"
# (builds 184 and 185, roadmap 100). This puts the dSYM whose UUID is the
# shipped binary's into ARCHIVE/dSYMs, so -exportArchive uploads it with the
# build, and copies it and the shipped binary into OUT_DIR for the workflow's
# testflight-symbols artifact.
#
# Never fails the build: without symbols the app is the same, only its crash
# reports are harder to read, and the workflow says so with a warning.
set -u

archive=${1:?archive}
builddir=${2:?build dir}
out=${3:?out dir}

shipped="$archive/Products/Applications/aether-mobile.app/aether-mobile"
mkdir -p "$out" "$archive/dSYMs"
[ -f "$shipped" ] && cp "$shipped" "$out/"

uuid_of() { dwarfdump --uuid "$1" 2>/dev/null | awk '{ print $2 }' | head -1; }

want=$(uuid_of "$shipped")
echo "symbols: the shipped binary's UUID ${want:-unknown}"

found=""
while IFS= read -r dsym; do
    dwarf="$dsym/Contents/Resources/DWARF/aether-mobile"
    [ -f "$dwarf" ] || continue
    uuid=$(uuid_of "$dwarf")
    echo "symbols: candidate $dsym, UUID ${uuid:-none}"
    if [ -n "$want" ] && [ "$uuid" = "$want" ]; then
        found=$dsym
        break
    fi
done < <(find "$archive/dSYMs" "$builddir" "$HOME/Library/Developer/Xcode/DerivedData" \
              -name 'aether-mobile.app.dSYM' -type d 2>/dev/null)

if [ -z "$found" ]; then
    echo "::warning title=No dSYM::no aether-mobile.app.dSYM with the shipped binary's UUID ${want:-unknown}: tester crash reports of this build will not name functions"
    ls -la "$out"
    exit 0
fi

inarchive="$archive/dSYMs/aether-mobile.app.dSYM"
if [ "$found" != "$inarchive" ]; then
    rm -rf "$inarchive"
    cp -R "$found" "$archive/dSYMs/"
fi
cp -R "$found" "$out/"

dwarf="$found/Contents/Resources/DWARF/aether-mobile"
symbols=$(nm "$dwarf" 2>/dev/null | wc -l | tr -d ' ')
size=$(du -sh "$dwarf" 2>/dev/null | awk '{ print $1 }')
echo "symbols: dSYM with the shipped UUID from $found ($symbols symbols, DWARF $size), now in the archive and in $out"
ls -la "$out"
exit 0
