#!/bin/bash
# patch-qt-usb-audio.sh QT_JAR_DIR ANDROID_JAR
#
# Compiles qt-patches/QtAudioDeviceManager.java (Qt 6.12.0's, with USB
# headsets known and a Bluetooth headset's call and music devices both
# listed) into QT_JAR_DIR/Qt6AndroidMultimedia.jar in place. Run by
# mobile/CMakeLists.txt when configuring for Android; does nothing when the
# jar has this version already (the mark is a string in the class: change it
# there and here with every change to the class, or a kept Qt install goes on
# with the old one).
set -euo pipefail
jar_dir="${1:?Qt jar directory}"
android_jar="${2:?android.jar}"
here="$(cd "$(dirname "$0")" && pwd)"
jar="$jar_dir/Qt6AndroidMultimedia.jar"
class="org/qtproject/qt/android/multimedia/QtAudioDeviceManager.class"
bin="${JAVA_HOME:+$JAVA_HOME/bin/}"
mark="OksaSDR audio patch 3"

if unzip -p "$jar" "$class" | grep -q "$mark"; then
    echo "already in $jar"
    exit 0
fi
work="$(mktemp -d)"
trap 'rm -rf "${work:?}"' EXIT
"${bin}javac" --release 17 -nowarn -cp "$android_jar:$jar_dir/Qt6Android.jar:$jar" -d "$work" \
    "$here/qt-patches/QtAudioDeviceManager.java"
(cd "$work" && "${bin}jar" uf "$jar" org/qtproject/qt/android/multimedia/QtAudioDeviceManager*.class)
unzip -p "$jar" "$class" | grep -q "$mark"
echo "patched $jar"
