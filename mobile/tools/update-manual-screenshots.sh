#!/usr/bin/env bash
# Renders the manual's screenshots (docs/mobile/manual/*.jpg).
#
#   mobile/tools/update-manual-screenshots.sh [RADIO_IP] [--only NAME,...]
#   mobile/tools/update-manual-screenshots.sh --list
#   mobile/tools/update-manual-screenshots.sh [RADIO_IP] [--only ...] --dry-run
#
# --only renders just those pictures (names as the files, without .jpg; e.g.
# --only mode-psk,tab-digi): only the runs that take them, and of the
# plans only their steps, a minute or two instead of the whole set's 15-18.
# --list prints every name. Names this script does not make are left to
# update-screenshots.sh (update-docs.sh passes --only to both). --dry-run
# prints the runs and their plans without running anything.
#
# As update-screenshots.sh: with RADIO_IP the pages show that FlexRadio,
# receive only (nothing is keyed); without it the built-in demo. Most
# pictures come from one run per window size (--shot-plan, ShotPlan.h:
# connect once, let the display fill, then open each chooser, take it and
# close it), a few minutes in all instead of 45 s a picture. The modes run
# and the last three change this app's own slice on the radio, receive only,
# and put it back on 14.074 DIGU: the modes, CW (the paddles), RADE on
# 14.236 (it makes this app's slice the TX slice; the run waits for the test
# to give TX back to the slice that had it) and FT8 decoding. Needs a
# configured AETHER_MOBILE build in build-mobile/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-mobile}"
OUT="$ROOT/docs/mobile/manual"
RADIO=""
ONLY=""
LIST=0
DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY="${2:?--only needs NAME,...}"; shift 2 ;;
        --list) LIST=1; shift ;;
        --dry-run) DRY=1; shift ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) RADIO="$1"; shift ;;
    esac
done

tabs=(dsp audio tx essb cw view profiles memory log digi freedv rtty)
# Each run and its pictures; infopanel, slicerow and spectrum are cut from
# overview. The modes run's segments (one per mode) run only when wanted.
ipad_shots=(overview infopanel slicerow spectrum filter agc antenna mode frequency step volume split
            network setup tnf noslice tx-meters meter)
phone_shots=(phone-portrait phone-menu memory-edit log-page ft8-hound)
for t in "${tabs[@]}"; do phone_shots+=("tab-$t"); done
modes_shots=(mode-usb mode-cw mode-am mode-fm fm-tx tx-lock mode-rtty rtty-panel tab-rtty digi-operating mode-psk)
amp_shots=(tab-amp amp-controls amp-confirm amp-tune setup-amp setup-txband setup-rx setup-usb meter-amp)
midi_shots=(setup-midi)
all_shots=(connect "${ipad_shots[@]}" ipad-portrait "${phone_shots[@]}" phone-landscape bandchooser
           "${modes_shots[@]}" "${amp_shots[@]}" "${midi_shots[@]}" tab-genius cw-paddles macros app-setups rade-on ft8-running)
if [ "$LIST" = 1 ]; then
    printf '%s\n' "${all_shots[@]}" | sort -u
    exit 0
fi
# want NAME...: true when any of them is to be rendered
want() {
    [ -z "$ONLY" ] && return 0
    local n
    for n in "$@"; do
        [[ ",$ONLY," == *",$n,"* ]] && return 0
    done
    return 1
}
DELAY="${SCREENSHOT_DELAY_MS:-40000}"
# How long a plan lets the display fill before its first picture.
FILL="${SCREENSHOT_FILL_MS:-30000}"
APP="$BUILD/mobile/aether-mobile"
if [ -n "$ONLY" ] && ! want "${all_shots[@]}"; then
    echo "No manual pictures among: $ONLY"
    exit 0
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ "$DRY" = 0 ]; then
    cmake --build "$BUILD" --target aether-mobile --parallel "$(nproc)" >/dev/null
fi
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
    if [ -n "$RADIO" ] && [ "$DRY" = 0 ]; then sleep 20; fi
}
shot() { # name size args...: one picture, one run (only when wanted)
    local name="$1" size="$2"
    shift 2
    want "$name" || return 0
    if [ "$DRY" = 1 ]; then
        echo "== run $name $size $*"
        return 0
    fi
    QT_QPA_PLATFORM=offscreen timeout 90 "$APP" --size "$size" \
        --screenshot "$TMP/$name.png" --screenshot-delay "$DELAY" "$@" >"$TMP/$name.log" 2>&1 || true
    echo "  $name ($size)"
    settle
}
plan() { # name size args...: the plan on stdin, many pictures in one run
    local name="$1" size="$2"
    shift 2
    cat > "$TMP/$name.plan"
    if [ "$DRY" = 1 ]; then
        echo "== plan $name $size $*"
        cat "$TMP/$name.plan"
        return 0
    fi
    QT_QPA_PLATFORM=offscreen timeout 300 "$APP" --size "$size" \
        --shot-plan "$TMP/$name.plan" --shot-dir "$TMP" "$@" >"$TMP/$name.log" 2>&1 || true
    echo "  $name: $(command grep -c '^shot ' "$TMP/$name.plan") pictures ($size)"
    settle
}
# jpg NAME [CROP] [RESIZE]: write docs/mobile/manual/NAME.jpg (when wanted)
jpg() {
    local name="$1" crop="${2:-}" resize="${3:-}"
    local args=()
    want "$name" || return 0
    [ "$DRY" = 1 ] && return 0
    if [ ! -f "$TMP/$name.png" ]; then
        echo "  MISSING: $name (the run did not take it; see its log)" >&2
        missing=1
        return 0
    fi
    [ -n "$crop" ] && args+=(-crop "$crop" +repage)
    [ -n "$resize" ] && args+=(-resize "$resize")
    magick "$TMP/$name.png" "${args[@]}" -strip -quality 85 "$OUT/$name.jpg"
}

echo "Rendering manual screenshots (${RADIO:-demo radio})${ONLY:+, only $ONLY}..."
missing=0
if want connect; then
    printf 'wait 3000\nshot connect\n' | plan connect 1366x1024
fi

# popup_shot NAME POPUP: open a chooser, take it, close it (when wanted)
popup_shot() {
    want "$1" || return 0
    printf 'popup %s\nshot %s\nclose\n' "$2" "$1"
}
if want "${ipad_shots[@]}"; then
    {
        echo "wait $FILL"
        if want overview infopanel slicerow spectrum; then echo "shot overview"; fi
        popup_shot filter filterChooser
        popup_shot agc agcChooser
        popup_shot antenna rxAntennaChooser
        popup_shot mode modeChooser
        if want frequency; then printf 'frequency-dialog\nshot frequency\nclose\n'; fi
        popup_shot step stepChooser
        popup_shot volume volumePopup
        popup_shot split splitChooser
        popup_shot network networkDialog
        popup_shot setup radioSetup
        popup_shot tnf tnfPopup
        if want noslice; then printf 'no-slice on\nshot noslice\nno-slice off\n'; fi
        # Two bars at the left, analog at the right, as while transmitting;
        # the meter chooser after it with both analog (styles are remembered).
        if want tx-meters; then
            printf 'meter-style two,analog\npreview-tx on\nshot tx-meters\npreview-tx off\nmeter-style analog,analog\n'
        fi
        if want meter; then
            want tx-meters || echo "meter-style analog,analog"
            printf 'popup meterChooser\nshot meter\nclose\n'
        fi
    } | plan ipad 1366x1024 "${src[@]}" --controls visible
fi

if want ipad-portrait; then
    printf 'wait %s\nshot ipad-portrait\n' "$FILL" | plan ipad-portrait 1024x1366 "${src[@]}" --controls visible
fi

if want "${phone_shots[@]}"; then
    {
        echo "wait $FILL"
        if want phone-portrait; then echo "shot phone-portrait"; fi
        if want phone-menu; then printf 'tap 0.93,0.03\nshot phone-menu\nclose\n'; fi
        if want memory-edit; then printf 'drawer 7\npopup memoryEditDialog\nshot memory-edit\nclose\n'; fi
        popup_shot log-page logPage
        for i in "${!tabs[@]}"; do
            # The Digi tab's picture comes from the modes run (RTTY on).
            [ "${tabs[$i]}" = rtty ] && continue
            if want "tab-${tabs[$i]}"; then printf 'drawer %s\nshot tab-%s\n' "$i" "${tabs[$i]}"; fi
        done
        # Hound on (it starts decoding), then off and decoding off again.
        if want ft8-hound; then
            printf 'drawer 9\ntapname digiHound\nwait 2500\nshot ft8-hound\ntapname digiHoundOff\nwait 800\ntapname digiDecode\nwait 1500\n'
        fi
    } | plan phone 440x956 "${src[@]}"
fi

if want phone-landscape; then
    printf 'wait %s\nshot phone-landscape\n' "$FILL" | plan phone-landscape 956x440 "${src[@]}" --controls visible
fi
if want bandchooser; then
    printf 'wait %s\npopup bandChooser\nshot bandchooser\n' "$FILL" | plan bandchooser 844x390 "${src[@]}"
fi

# Each mode on a phone (Operating by mode): this app's active slice tuned
# to a frequency for the mode, receive only, the waterfall given time to
# fill; the TX tab's FM repeater section and Lock row; RTTY with a test
# text through the decoder, its panel and the Digi tab with RTTY on (the
# top, then the calls heard and macros); PSK31 on 14.0695 with a test
# signal 6 Hz off the carrier. Each mode's steps only when one of its
# pictures is wanted. Back on 14.074 DIGU after.
if want "${modes_shots[@]}"; then
    {
        printf 'ready\nwait 20000\n'
        if want mode-usb; then printf 'freq 14.200\nmode USB\nwait 20000\nshot mode-usb\n'; fi
        if want mode-cw; then printf 'freq 14.030\nmode CW\nwait 20000\nshot mode-cw\n'; fi
        if want mode-am; then printf 'freq 7.150\nmode AM\nwait 20000\nshot mode-am\n'; fi
        if want mode-fm fm-tx tx-lock; then
            printf 'freq 29.600\nmode FM\nwait 20000\n'
            if want mode-fm; then echo "shot mode-fm"; fi
            printf 'drawer 2\nwait 1500\nscroll 0\nwait 800\nshot fm-tx\nscroll 550\nwait 800\nshot tx-lock\nclose\n'
        fi
        if want mode-rtty rtty-panel tab-rtty digi-operating; then
            printf 'freq 14.085\nmode RTTY\nwait 15000\n'
            echo "rttyfeed CQ CQ DE OH2XYZ OH2XYZ OH2XYZ PSE K"
            printf 'wait 8000\nshot mode-rtty\npopup rttyPanel\nwait 1500\nshot rtty-panel\nclose\n'
            printf 'drawer 11\nwait 1500\nscroll 0\nwait 800\nshot tab-rtty\nscroll 200\nwait 800\nshot digi-operating\nclose\n'
        fi
        if want mode-psk; then
            printf 'textmode PSK31\nwait 2000\nfreq 14.0695\nwait 15000\ntextclear\n'
            echo "pskfeed +6 CQ CQ CQ de OH2XYZ OH2XYZ OH2XYZ pse k"
            printf 'wait 14000\nshot mode-psk\ntextmode RTTY\nwait 2000\n'
        fi
        printf 'freq 14.074\nmode DIGU\nwait 3000\n'
    } | plan modes 440x956 "${src[@]}"
fi

# The Amp tab (with Control on), its confirmation, Radio Setup › Amp and
# Transmit's TX band settings, and the meters with the amplifier's
# readings, against a stand-in RF2K-S (rf2k-mock.py, transmitting, antennas
# 1-3 set up, on this computer's own network address and the API's port
# 8080, so the address reads as an amplifier's would). Nothing is sent to
# it and nothing is keyed (the confirmations, Tune's too, are cancelled);
# the meters get their choices back and the app forgets the amplifier after.
if want "${amp_shots[@]}" && [ "$DRY" = 0 ]; then
    lan="$(ip -4 route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p')"
    python3 "$ROOT/mobile/tools/rf2k-mock.py" --bind "${lan:-127.0.0.1}" --port 8080 --keyed --operate \
        --antennas 1,2,3 --name Shack >"$TMP/rf2k-mock.log" 2>&1 &
    mock=$!
    sleep 1
    if ! kill -0 "$mock" 2>/dev/null; then
        echo "  the RF2K-S stand-in did not start (port 8080 taken?): $(tail -1 "$TMP/rf2k-mock.log")" >&2
    fi
fi
if want "${amp_shots[@]}"; then
    {
        printf 'ready\namp %s\nwait 4000\n' "${lan:-127.0.0.1}"
        # Control on, as the user turns it on (the Amp tab's rows need it).
        printf 'popup radioSetup\nwait 800\nclick setupTab7\nwait 1500\ntapname ampControlOn\nwait 1000\n'
        if want setup-amp; then printf 'shot setup-amp\n'; fi
        if want setup-txband; then printf 'click setupTab1\nwait 1500\nshowname txBandHwAlcOn\nwait 800\nshot setup-txband\n'; fi
        # The radio's receiver: calibration, reference and filters (shown).
        if want setup-rx; then printf 'click setupTab4\nwait 2000\nshot setup-rx\n'; fi
        # The radio's own USB cables, as they are (only shown, nothing set).
        if want setup-usb; then printf 'click setupTab8\nwait 2000\nshot setup-usb\n'; fi
        printf 'close\nwait 600\n'
        if want tab-amp amp-controls amp-confirm amp-tune; then printf 'drawer 12\nwait 1500\nscroll 0\nwait 800\n'; fi
        if want tab-amp; then printf 'shot tab-amp\n'; fi
        if want amp-controls amp-confirm amp-tune; then printf 'showname ampReset\nwait 800\n'; fi
        if want amp-controls; then printf 'shot amp-controls\n'; fi
        if want amp-confirm; then printf 'tapname ampAntenna2\nwait 1000\nshot amp-confirm\ntapname ampConfirmNo\nwait 600\n'; fi
        if want amp-tune; then printf 'tapname ampTune\nwait 1000\nshot amp-tune\ntapname ampConfirmNo\nwait 600\n'; fi
        if want tab-amp amp-controls amp-confirm amp-tune; then printf 'close\nwait 600\n'; fi
        if want meter-amp; then
            # Amp power at the right and amp SWR at the left in transmit,
            # shown with the transmit preview; then the radio's Power and
            # SWR back, as the other pictures have them.
            printf 'tapname meterRight\nwait 800\ntapname meterKindAmppowerTx\nwait 400\nclose\nwait 500\n'
            printf 'tapname meterLeft\nwait 800\ntapname meterKindAmpSWRTx\nwait 400\nclose\nwait 500\n'
            printf 'preview-tx on\nwait 2000\nshot meter-amp\npreview-tx off\nwait 800\n'
            printf 'tapname meterRight\nwait 800\ntapname meterKindPowerTx\nwait 400\nclose\nwait 500\n'
            printf 'tapname meterLeft\nwait 800\ntapname meterKindSWRTx\nwait 400\nclose\nwait 500\n'
        fi
        printf 'amp off\nwait 1000\n'
    } | plan amp 440x956 "${src[@]}"
    if [ "$DRY" = 0 ]; then
        kill "$mock" 2>/dev/null || true
        wait "$mock" 2>/dev/null || true
        # Nothing sent: the stand-in counts any request that would change it.
        tail -1 "$TMP/rf2k-mock.log" | sed 's/^/  RF2K-S stand-in: /'
    fi
fi

# Radio Setup › MIDI against a stand-in CTR2-MIDI (ctr2-mock, an ALSA virtual
# MIDI port on this computer), chosen as the user chooses it; nothing is
# learned and the stand-in sends nothing. The functions shown are this
# computer's own MIDI bindings (~/.config/AetherSDR/midi.settings).
if want "${midi_shots[@]}" && [ "$DRY" = 0 ]; then
    if [ ! -x "$ROOT/build-mobile/ctr2-mock" ]; then
        g++ -O2 -D__LINUX_ALSA__ -I"$ROOT/third_party/rtmidi" "$ROOT/mobile/tools/ctr2-mock.cpp" \
            "$ROOT/third_party/rtmidi/RtMidi.cpp" -lasound -lpthread -o "$ROOT/build-mobile/ctr2-mock" 2>/dev/null
    fi
    "$ROOT/build-mobile/ctr2-mock" sleep:240000 >"$TMP/ctr2-mock.log" 2>&1 &
    ctr2=$!
    sleep 1
fi
if want "${midi_shots[@]}"; then
    printf 'ready\npopup radioSetup\nwait 800\nclick setupTab9\nwait 1500\ntapname midiPort_CTR2MIDI\nwait 1500\nshot setup-midi\n' \
        | plan midi 440x956 "${src[@]}"
    if [ "$DRY" = 0 ]; then
        kill "$ctr2" 2>/dev/null || true
        wait "$ctr2" 2>/dev/null || true
    fi
fi

# The Amp tab with a 4O3A Power Genius XL and Tuner Genius XL: the radio's
# device status stood in for by the render tool (radiostatus), the devices'
# direct links by genius-mock.py on this machine. Nothing reaches a device
# and nothing is keyed; control stays off, so no button can send.
if want tab-genius; then
    glan="$(ip -4 route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p')"
    glan="${glan:-127.0.0.1}"
    if [ "$DRY" = 0 ]; then
        python3 "$ROOT/mobile/tools/genius-mock.py" --bind "$glan" >"$TMP/genius-mock.log" 2>&1 &
        gmock=$!
        sleep 1
    fi
    printf 'ready
wait 2000
%s
%s
wait 4000
drawer 12
wait 2500
scroll 0
wait 800
shot tab-genius
close
wait 600
' \
        "radiostatus amplifier 0x00000001 model=PowerGeniusXL ip=$glan serial_num=MOCK ant=ANT1:PORTA,ANT2:NONE state=STANDBY" \
        "radiostatus amplifier 0x00000002 model=TunerGeniusXL ip=$glan serial_num=MOCKT operate=1 bypass=0 tuning=0 relayC1=44 relayL=12 relayC2=8 antA=0 one_by_three=1 ant=ANT1,ANT2" \
        | plan genius 440x956 "${src[@]}"
    if [ "$DRY" = 0 ]; then
        kill "$gmock" 2>/dev/null || true
        wait "$gmock" 2>/dev/null || true
    fi
fi

# Situations (they change this app's slice and put it back: CW, then
# DIGU again; RADE and FT8 restore what they found, 14.074 DIGU).
if want cw-paddles; then
    printf 'ready\nmode CW\nwait 25000\nshot cw-paddles\nmode DIGU\nwait 2000\n' \
        | plan cw-paddles 440x956 "${src[@]}" --cw-paddles on
fi
# The Macros panel in CW (the CW memories), opened by its button and closed
# again; nothing is sent.
if want macros; then
    printf 'ready\nmode CW\nwait 6000\ntapname macrosButton\nwait 1500\nshot macros\ntapname macrosButton\nwait 600\nmode DIGU\nwait 2000\n' \
        | plan macros 440x956 "${src[@]}"
fi
# The Prof tab's App setups (roadmap 17), scrolled to them; nothing saved.
if want app-setups; then
    printf 'ready\ndrawer 6\nwait 1500\nscroll 2000\nwait 1000\nshot app-setups\nclose\nwait 600\n' \
        | plan app-setups 440x956 "${src[@]}"
fi
# The tests end their runs: RADE on 10 s after the start, the picture at
# 40 s, RADE off with the slice and the TX slice back at 42 s; FT8 put back
# at 53 s (not 40 s: the test reads a "40" as 40 m). Each run's log says
# what it restored.
shot rade-on 440x956 "${src[@]}" --rade-test 32,14.236
if want rade-on && [ "$DRY" = 0 ]; then
    command grep -h "rade-test: restored" "$TMP/rade-on.log" | sed 's/^/  /' \
        || echo "  rade-on: no restore line in its log: check the TX slice" >&2
fi
shot ft8-running 440x956 "${src[@]}" --ft8-test 45
if want ft8-running && [ "$DRY" = 0 ]; then
    command grep -h "ft8-test: restored" "$TMP/ft8-running.log" | sed 's/^/  /' \
        || echo "  ft8-running: no restore line in its log: check the slice" >&2
fi

jpg connect "" 900x
jpg overview "" 1100x
jpg phone-portrait
jpg phone-landscape
if [ -f "$TMP/overview.png" ]; then
    cp "$TMP/overview.png" "$TMP/infopanel.png"
    cp "$TMP/overview.png" "$TMP/slicerow.png"
    cp "$TMP/overview.png" "$TMP/spectrum.png"
fi
jpg infopanel 500x95+485+45
jpg slicerow 985x140+0+205
jpg spectrum 985x600+0+340 800x
jpg filter 620x420+470+40
jpg agc 560x470+470+40
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
jpg macros 440x456+0+500
jpg app-setups 440x236+0+636
jpg rade-on 440x620+0+0
jpg ft8-running "" 330x
jpg tx-meters 985x140+0+200
jpg frequency 700x720+333+150 500x
jpg noslice 985x600+0+340 800x
jpg network 460x600+448+212
jpg setup 640x600+363+50
jpg tnf 967x492+13+354 800x
jpg memory-edit 440x380+0+280
# Its buttons only: the log lines below them are the radio's own.
jpg log-page 418x156+11+110
for t in "${tabs[@]}"; do
    jpg "tab-$t" 440x520+0+436
done
for m in usb cw am fm rtty psk; do
    jpg "mode-$m" "" 330x
done
jpg ft8-hound 440x520+0+436
jpg tab-amp 440x520+0+436
jpg tab-genius 440x520+0+436
jpg setup-amp 418x700+11+40 330x
jpg setup-txband 418x700+11+40 330x
jpg setup-rx 418x825+11+40 330x
jpg setup-usb 418x700+11+40 330x
jpg setup-midi 418x825+11+40 330x
jpg amp-controls 440x520+0+436
jpg amp-confirm 440x420+0+395
jpg amp-tune 440x432+0+383
jpg meter-amp 440x100+0+208
jpg rtty-panel 440x440+0+172
jpg digi-operating 440x332+0+536
jpg fm-tx 440x160+0+532
jpg tx-lock 440x110+0+596
if [ "$missing" = 1 ]; then
    echo "Some pictures are missing (above): their old files are kept." >&2
    exit 1
fi
echo "Updated $OUT${ONLY:+ ($ONLY)}"
