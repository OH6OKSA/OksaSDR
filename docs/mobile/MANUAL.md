# OksaSDR — User Manual

OksaSDR turns an iPad, iPhone, Mac or Android phone or tablet into a
complete FlexRadio client: panadapter and waterfall, all receive controls,
transmit (voice, CW, tune), FT8 / FT4 and RADE digital voice, a log, and
SmartLink or VPN remote operation, with no computer alongside.

This manual describes every function and how it works. The screenshots are
taken from the app connected to a real FLEX radio. The app's **Manual**
button (top row) opens this page.

- [1. Connecting](#1-connecting)
- [2. The main screen](#2-the-main-screen)
- [3. Frequency, mode and the info panel](#3-frequency-mode-and-the-info-panel)
- [4. Meters and slices](#4-meters-and-slices)
- [5. Panadapter and waterfall](#5-panadapter-and-waterfall)
- [6. Split](#6-split)
- [7. Transmitting: PTT, KEY and TUNE](#7-transmitting-ptt-key-and-tune)
- [8. Controls](#8-controls)
- [9. Radio Setup](#9-radio-setup)
- [10. Notices and problems](#10-notices-and-problems)
- [11. Keyboard and Apple Pencil](#11-keyboard-and-apple-pencil)
- [12. Reporting a problem](#12-reporting-a-problem)
- [13. Android](#13-android)

---

## 1. Connecting

![Connection page](manual/connect.jpg)

The first page lists the ways to reach a radio:

- **On this network** — FlexRadios found on the local network (on an
  iPhone or iPad, connect by IP address for now). Tap a radio's card, or its
  **Connect ›**, to connect. *AetherSDR Demo* is a built-in receive-only demo radio for trying
  the app without a radio.
- **SmartLink (remote)** — sign in with your FlexRadio SmartLink account
  to use your radio from anywhere. The sign-in is kept on this device only
  (the iOS Keychain; on Android, encrypted under a key in the Android
  Keystore); the password is never stored. **Sign out** takes two taps
  (the first turns it into *Sign out?* for three seconds), so a stray tap
  does not sign you out.
- **By IP address** — tap the address field and enter your radio's IP
  address on the keypad (SmartSDR's radio chooser and your router's device
  list both show it), then *Connect*. A hardware keyboard works in the
  keypad too.
  The device must reach the radio on the same network or over a VPN.

The top row shows the app version, **Manual** and **Log** (see
[Reporting a problem](#12-reporting-a-problem)).

When you connect, the radio's microphone input is switched to *PC* so the
device's microphone can be used for transmit.

## 2. The main screen

![Main screen on an iPad](manual/overview.jpg)

From top to bottom. Throughout the app a button that is **on** (a chosen
mode, a DSP that runs, the shown tab) is **lit blue**; the others are dark.


| Area | What it is |
|---|---|
| Top row | Radio name, [network quality](#network-quality), app version, **Controls** (open or hide the controls), **Setup** ([Radio Setup](#9-radio-setup)), **Manual**, **Log**, **Disconnect** (on a phone Setup, Manual, Log and Disconnect are behind **⋯**, and the controls open from the button beside PTT) |
| Frequency row | Frequency, mode, and the [info panel](#3-frequency-mode-and-the-info-panel) |
| Band buttons | 160 m … 6 m, the radio's transverter (XVTR) bands, WWV, GEN |
| Slice row | The two [meters](#4-meters-and-slices) at the ends, the slices in between |
| Panadapter and waterfall | The [spectrum display](#5-panadapter-and-waterfall) |
| Step buttons | Tune by −1 kHz … +1 kHz (with an FM step: the step, 100 kHz, 1 MHz, 10 MHz) |
| PTT / KEY | [Transmit](#7-transmitting-ptt-key-and-tune) |

On an iPad or tablet the controls can stay pinned at the side (as above) or
open as a drawer. On a phone they open as a drawer from the **Controls**
button beside PTT (three bars in landscape); the drawer carries its own PTT
in the same place. A swipe inside the open drawer never tunes; the part of
the display still in view tunes as usual.

| iPhone, portrait | iPhone, landscape |
|---|---|
| ![iPhone portrait](manual/phone-portrait.jpg) | ![iPhone landscape](manual/phone-landscape.jpg) |

On a phone, **⋯** at the top right holds Radio Setup, Manual, Log and
Disconnect:

![The phone's ⋯ menu](manual/phone-menu.jpg)

The layout follows the device: rotation, Split View, Stage Manager and
folding switch it live. On a very low screen in landscape (a phone's outer
screen, the smaller iPhones) the band buttons give way to the display;
there the **Band** button on the waterfall opens the bands, with *Whole
band* (the band zoom) under them.

![The bands from the Band button in a low landscape window](manual/bandchooser.jpg)

### Network quality

The dot in the top row (with the app's CPU next to it) rates the connection as the desktop AetherSDR does —
**Excellent / Very Good** (green), **Good** (blue), **Fair** (amber),
**Poor** (red) — with the round-trip time. On an iPhone the dot, the time
and the CPU show. Tap it for the details:

![Network details](manual/network.jpg)

- **Link timing** — round trip to the radio, audio jitter, and the gap
  between audio packets (current and longest).
- **Packet integrity** — packets lost in the last 10 s and in the session,
  per stream (audio, spectrum, waterfall, meters). Packets that merely
  arrive out of order are not counted as lost.
- **Traffic** — receive and send rates and totals.
- **Performance** — the app's CPU as a share of all cores (also shown in the
  top row), the same added up over all its threads with one core = 100 %
  (102 % is about one core's worth spread over several threads, not one
  core full), on Android the **busiest threads** (UI, render, audio…; one
  near 100 % is a core at its limit), and the screen frames drawn per
  second. iOS and Android give apps no GPU load; frames drawn falling well
  below the display's rate means the graphics are not keeping up.

## 3. Frequency, mode and the info panel

**Frequency.** Tap the frequency for direct entry with a keypad (or type on
a hardware keyboard), then *Tune*. *14.074* and *14074* both tune
14.074 MHz; on a transverter band, *439.000* (or *439*) tunes 439 MHz.

![Frequency entry](manual/frequency.jpg)

**Mode.** Tap the mode (e.g. *USB ▼*) and choose another.

![Mode chooser](manual/mode.jpg)

**STEP** (under the mode, e.g. *STEP 100 Hz*) is the tuning step: tap it to
choose from the radio's list. Tap-tune and the arrow keys use it; Apple
Pencil drags tune exactly, off the step's grid.

![Tuning step chooser](manual/step.jpg)

**Band buttons.** Each band returns to the frequency and mode you last used
there. The radio keeps these in its *band stack*, which is part of the
radio's global profile, so a different profile can bring a band back
elsewhere. **WWV** holds the time-signal frequencies and **GEN** general
coverage, each with its own band-stack memory. Transverters set up in the
radio (in SmartSDR or the AetherSDR desktop: Radio Setup → XVTR) get a
button of their own after 6 m, named as in the radio (for example 2m): the
radio tunes the RF frequency, uses the transverter port and does the IF
itself. On a phone the band row scrolls sideways.

**Step buttons.** ±1 Hz … ±1 kHz, landing on round frequencies: +10 Hz from
14.100 002 goes to 14.100 010, not 14.100 012. With a step of 5 kHz or
more (STEP, e.g. an FM channel step of 12.5 kHz on 70 cm) they become ±STEP, ±100 kHz,
±1 MHz and ±10 MHz, all on the step's grid. The band button of the band
you are on is lit.

### The info panel

![Info panel](manual/infopanel.jpg)

The active slice at a glance, after the slice flag of SmartSDR. Every item
is a touch target with its choices behind it:

| Item | Shows | Tap |
|---|---|---|
| **A** (blue) | Slice letter | — |
| **TX** (red) | This slice transmits | — |
| **ANT1** (blue) | Receive antenna | Choose the RX antenna |
| **ANT1** (red) | Transmit antenna | Choose the TX antenna |
| **2.7K** | Receive filter width | Filters and DSP |
| **AGC-M** | AGC mode | AGC mode, AGC threshold and RF gain |
| Speaker | Audio on (blue) / muted (red); the ramp beside it fills with this slice's volume (full at 100; a new slice starts at 50) | Every slice's volume (AF gain), Mute and pan (left–right, read as C, L n or R n; it snaps to centre near the middle, and double-tap centres) |
| Lock | Tuning lock open / locked | Lock or unlock tuning (flashes when a tune is refused) |
| **SPLIT** | Split on (amber) or off (grey); shown while the radio can transmit and has a slice free for it | Start or end [split](#6-split); hold to pick the offset |
| **QSK** | In CW modes: break-in on (bright) or off | — |
| **NB NR ANF NRL …** | Every DSP the radio offers, bright when on (on an iPhone the rarer ones only while on) | Filters and DSP |

The speaker opens every slice's volume, mute and pan:

![Volume, mute and pan of every slice](manual/volume.jpg)

The settings that are always there come first; split and the DSP flags
follow on a second row. On an iPhone in portrait STEP and the antennas, width and AGC sit beside the
frequency and the rest under it; in landscape it is one row, STEP first.

**Filters and DSP** — the mode's eight filter widths (the active one lit;
choosing one closes the window) and the radio's DSP: NR, NB, ANF, APF (CW),
and NRL, ANFL, ANFT, NRS, RNN, NRF where the radio has them, and **WNB**, the
panadapter's wideband noise blanker. **BC** is the app's own beat cancel
(below). DSP toggles leave the window open, so several can be set together.
Each DSP that is on and has a level (NR, NB, ANF, APF, NRL, ANFL, NRS, NRF,
WNB) gets a slider under the toggles, sent to the radio when released.

![Filters and DSP](manual/filter.jpg)

**BC — beat cancel**, as on Kenwood rigs: finds steady carriers in the
receive audio — someone tuning up on your frequency — and notches them out
within about 0.15 s, leaving voice alone; it adds no delay to the audio. Up
to four carriers at a time. Voice modes only: in CW and data modes (and
whenever another audible slice is in one) it is off and its button shows
dimmed, since it would remove the signal itself; back in a voice mode it
returns as you left it.

**AGC** — off / slow / med / fast, the AGC threshold (AGC-T) and the
panadapter's RF gain (preamp or attenuator, in the radio's steps). When
the app changes the mode (the mode chooser, a band button, an FTx band
button, RADE), AGC follows it: **fast** for CW and data (DIGU, DIGL,
RTTY, FreeDV), **slow** for voice. Pick another AGC and the app keeps it
for that kind of mode from then on. A mode change made by another program
on the radio leaves the AGC alone.

![AGC, AGC-T and RF gain](manual/agc.jpg)

**Antennas** — the antenna ports the radio offers for receive or transmit.

![Antenna chooser](manual/antenna.jpg)

## 4. Meters and slices

![Slice row](manual/slicerow.jpg)

**Meters.** Two meters, at the ends of the slice row. Tap a meter to choose
its style and what it shows — separately for receive and for transmit (e.g.
S, then SWR on the air). Choices are remembered. Each meter has one of
three styles:

- **Analog** — an arc and a needle; it stays analog while transmitting too.
- **Bar** — one bar.
- **Two bars** — two bars, one over the other, each with its own receive
  and transmit choice: tap the upper or the lower bar to choose what that
  bar shows. Two bars at the left give three readings on the air, e.g. SWR
  and Mic at the left and Power at the right (four with two bars at both
  ends).

Each bar has the analog meter's scale under it — S1…S9 then +20/+40/+60 in
red on the S meter, five values with the alarm range in red on the others —
and the reading's name and value above it. A low bar (iPhone landscape)
keeps the ticks and leaves out the numbers.

![Meters while transmitting](manual/tx-meters.jpg)

![Meter chooser](manual/meter.jpg)

Receive offers S, Mic, PA temperature and supply voltage; transmit adds
Power, SWR, ALC and Compression, which the radio reads only while
transmitting. **Mic** is on the left meter and its lower bar
(on a tablet the side panel covers the right one). With this device's microphone it shows the
level of the audio as it is sent (after the processing in Controls → TX):
while you transmit, and in receive too while Controls → TX is open, so the
level can be set before going on the air; with a microphone on the radio
it is the radio's own meter.

**Peak hold.** Each meter keeps its highest reading — an amber needle on
the analog meter, an amber marker across the bar — then lets it fall back,
so a voice peak or a burst of noise can be read. *Peak hold* in the meter's
chooser sets it for all meters, as the desktop S meter's decay: *Off*,
*Fast* (0.2 s, then 20 dB/s), *Medium* (0.5 s, then 10 dB/s; the
default) or *Slow* (1 s, then 5 dB/s); other scales fall in the same time
for their span.

**Slices.** Each slice is a button between the meters ("A 14.100 USB"; on a
phone without the mode, which is beside the frequency); the
transmit slice carries a red **TX** tag. Two fit side by side, four make
2 × 2.

- Tap a slice to make it the active one (tuning, info panel and controls
  follow it).
- Press and hold for **Make TX slice** and **Close slice**.
- **+ Slice** and **+ Pan** (bottom right of the waterfall) add a slice or a
  panadapter.
- Closing the transmit slice hands transmit to the slice that stays, so PTT
  keeps working.

## 5. Panadapter and waterfall

![Panadapter and waterfall](manual/spectrum.jpg)

- **Tap** to tune to the touched point (on the tuning step).
- **Drag inside the filter passband** to tune; the slice follows the finger
  in tuning steps (STEP), so it stops on a round frequency.
- **Drag a passband edge** (a finger's width around either edge line) to
  set the filter in 10 Hz steps, for any width; the filter chooser's eight
  widths stay as they were.
  Holding the finger at either edge keeps tuning, faster nearer the edge.
- **Drag anywhere else** to move the view; the frequency stays.
- **Pinch**, or **− / +** (bottom left), to zoom around the slice.
- **Band** (beside − / +) zooms out to the whole band you are on; tap it
  again to return to the slice.
- The **frequency scale** runs along the top of the waterfall.
- The **dBm scale** at the left: drag it up or down to move the trace (the
  reference level; this turns the spectrum's *Auto* off, View gives it
  back). In 3D, as on the desktop, the 3D levels stay automatic:
  the floor (the 3D zero) follows the noise and the scale runs from it to
  the top. Dragging sets **3D floor**, how far under the noise the zero
  sits (0–24 dB, default 6): drag up and the noise rises, down and it
  sinks. The same setting is a slider in View.
- The **divider** between the spectrum and the waterfall (a short bar on
  the line, right of centre): drag it up or down to give the spectrum or
  the waterfall more room, as on the desktop. The new split applies when
  you lift your finger (a line shows where it goes; the waterfall starts
  over at its new size) and is remembered. Double-tap it for the
  automatic split.
- **RF GAIN +8 dB** (top right) shows the RF gain; tap it for AGC, AGC-T and
  RF gain. A gain step shows on the
  waterfall and trace, as on the desktop.
- The **slice** is a white dashed line at its frequency in data, CW and
  RTTY modes, with the frequency beside it at the foot of the spectrum; in
  voice modes (USB, LSB, AM, FM) the line is left out, as the passband's
  edge marks the carrier there, and the frequency stays beside it. The
  **filter passband** is the shaded band between two solid yellow edges,
  on the correct side for USB/LSB (in LSB the carrier is at its right edge,
  in USB at its left) and centred for CW/AM. Other slices are marked with their letter;
  tap one to select it.
- Your own transmission shows in the waterfall.
- In CW, decoded text runs along the top of the waterfall.
- **Spots** — callsigns along the top of the spectrum at their frequency:
  the spots the radio holds, from this app's DX cluster connection and
  from any other program on the radio. Tap one to tune to it (the mode
  follows the spot, as on the desktop). Close ones stack in up to four
  rows; zoom in to see more. In View, *Spots* shows or hides them, and
  *DX cluster* connects the app to a cluster of your choice.
- **Notches (TNF)** — **press and hold** on the display to put a notch
  there, or *+ TNF* in Controls → DSP for one in the middle of the
  passband. A notch shows as a band through spectrum and waterfall
  (yellow, or green when permanent) with a handle on top outlined in black:
  filled while TNF is on, hollow while it is off. Tap it for its
  width (50–500 Hz), depth, *Permanent*, a 10 Hz nudge either way and
  *Remove*; *TNF on/off* there is the radio's switch for all notches —
  they only work while it is on. Drag a notch along the spectrum (not the
  waterfall) to move it.

![Notch settings](manual/tnf.jpg)

**View** (in Controls) switches between the 2D waterfall and the **3D**
stacked trace and sets the levels. They work as on the desktop AetherSDR:

- **Spectrum** — *Auto* keeps the noise floor a quarter of the way up and
  follows it calmly: it rises only once a change has lasted over a second
  (a lightning crash does not lift it), falls quickly, and re-locks after a
  retune or zoom. **Range** is the height of the scale, in Auto too; with
  Auto off, **Reference** is the top. The trace is lightly averaged.
- **Waterfall** — *Auto* puts black at the noise floor; **Black level**
  moves it (50 = at the noise, higher shows more of it). **Colour gain**
  sets how many dB the colours span, so the same signal looks the same on
  any band and a strong carrier does not darken the rest. During and just
  after a transmission the levels hold.
- **FFT average**, **FFT frames/s** and **Waterfall speed** are the
  radio's own panadapter settings (as in SmartSDR): averaging smooths the
  trace on the radio, frames/s sets how often the trace updates, and the
  waterfall speed how fast lines scroll. Each is sent when the slider is
  released and kept by the radio.
- **3D** — the surface takes a row with each waterfall line and glides
  between them. **3D smoothing**: *Off* draws the rows as live as the 2D
  trace, *Light* (default) blends a little with the previous row so the
  narrow dips (say, gaps in your own voice's spectrum) stay visible,
  *Desktop* smooths as the desktop does (no flicker from single spikes,
  a calm surface). Ridges
  are soft lines, the near rows widen over the side gaps where the radio's
  wider waterfall data reaches, and the slices' passbands and carriers are
  shaded into the surface in perspective. Its height is the spectrum's
  Range; **3D floor** sets how far under the noise its zero sits and
  **3D gain** how far down the colours reach. In portrait the 3D keeps
  landscape's shape and the waterfall gets the rest of the height. It keeps
  its history while you are in 2D. If the app stopped while the 3D view was
  on, or the 3D view runs too slowly on the device, the app starts or goes
  on with the 2D waterfall and says so; turn 3D on again in View.

**Panadapters.** The display shows one panadapter at a time: the one of
the active slice. Selecting a slice on another panadapter (its chip in the
slice row) switches the display to it. With more than one open, **Close N
other pans** on the display closes the others.

## 6. Split

Tap **SPLIT** on the info panel. As on SmartSDR and AetherSDR, a second slice
is created on the same panadapter for transmit:

- 1 kHz above in CW, 5 kHz above in other modes, same mode;
- it is muted (you listen on the original slice) and takes transmit;
- it becomes the active slice, so you can tune the transmit frequency at
  once (drag its passband, or the step buttons).

Tap **SPLIT** again — or close either slice — to end split: the transmit
slice closes and transmit returns to the receive slice.

**Press and hold SPLIT** to choose the offset: **+1 kHz**, **+5 kHz** or
**+10 kHz** above the receive frequency. While split is on, the same menu
offers **Off**.

![Split offset chooser](manual/split.jpg)

## 7. Transmitting: PTT, KEY and TUNE

- **PTT** — hold to transmit from the device's microphone; release to stop.
  PTT is for voice: with the TX slice in DIGU or DIGL it says so and does
  not key (the radio refuses voice there); switch to USB / LSB, or use the
  FTx tab for FT8 / FT4 and the FreeDV tab for RADE. When the radio refuses transmit, its own reason
  is shown.
  The microphone is open only while transmitting. **Tap on / off** (Controls
  → TX → PTT) makes it a switch: tap to transmit, tap again to stop (TX ■
  while it is on); as a safety it stops by itself after 3 minutes. Space on
  a keyboard works the same way.
- **KEY** — in CW modes the same button is a straight key, or **paddles**
  (Controls → CW → Key: *Paddles*): dit and dah side by side in its
  place, each its own touch, so holding both gives iambic squeeze. The app
  times the elements itself, sends them to the radio and plays the
  sidetone in step. **Iambic** in the CW tab sets **A** or **B** (B adds
  one more element after a squeeze) and **Swap ⇄** (dah on the left); they
  are the radio's settings, also in [Radio Setup](#9-radio-setup) →
  Phone/CW. **Paddles on a keyboard** picks which two keys of a hardware
  keyboard are the paddles, the left one dit and the right one dah:
  left and right **Ctrl** (the default; what USB paddle adapters such as
  Vail and VBand send), left and right **Alt ⌥** (Option; for keyboards with no right
  Ctrl, such as Apple's Magic Keyboard), **[ ]** (on English layouts,
  where [ and ] are keys of their own; on Finnish or German layouts they
  need AltGr or Option, so pick Ctrl or Alt there), or **Off**.

  ![CW paddles in PTT's place](manual/cw-paddles.jpg)

  For a straight key, the sidetone
  plays on this device as you key (at the radio's CW pitch and monitor
  level, when the radio's sidetone is on), without the round trip to the
  radio.
- **TUNE** (Controls → TX) sends a carrier at the tune power; while it is on,
  the big button reads TUNE and pressing it stops the carrier.

The button is always on top. On a phone an open controls drawer carries its
own copy at the bottom, beside **Close** (which closes the drawer); on a
tablet the drawer stops above the page's PTT, so nothing ever covers it.
When transmit is not possible (no TX slice, the radio's mic input not
this device, a refusal), the reason shows above PTT and under the
controls; on a phone the power and SWR show beside PTT while you
transmit.

Safety: transmit goes through AetherSDR's TX controller. The transmitter
unkeys on release, on disconnect, when the app leaves the screen, when the
layout changes and when the microphone input leaves the device. If the radio
refuses to transmit, its reason pops up (for example *No transmit slice is
assigned* or *The selected TX antenna cannot transmit on this frequency*).

## 8. Controls

**Controls** (top row) opens the controls; on an iPad, *Pin Controls* keeps
them in the page (at the right in landscape, under the display in portrait),
with toggle buttons to show several sections at once; *Hide controls* puts
them back into the drawer. The buttons are all one size: **DSP**, **Audio**,
**TX**, **CW**, **View**, **Prof** (profiles), **Mem** (memory), **Log**,
**FTx** (FT8 / FT4) and **FreeDV** (RADE digital voice and FreeDV
Reporter).

![iPad in portrait with the controls pinned under the display](manual/ipad-portrait.jpg)

| DSP | Audio | TX |
|---|---|---|
| ![DSP](manual/tab-dsp.jpg) | ![Audio](manual/tab-audio.jpg) | ![TX](manual/tab-tx.jpg) |

- **DSP** — RIT and XIT (on/off; − and + step 10 Hz and run while held,
  faster the longer: 10, then 50, then 100 Hz; tap the value to reset;
  not in FM), squelch (where the radio has it), and **TNF** (the radio's notch filters on/off, *+ TNF* for a
  notch in the passband; see [Panadapter and waterfall](#5-panadapter-and-waterfall)).
- **Audio** — **Receive audio**: *Auto* uses Opus (compressed, made for
  the internet) over SmartLink or when the radio is reached through a VPN,
  and uncompressed audio on the local network; *Opus* or *Uncompressed* fix
  it. It shows what the current connection uses; a change applies from the
  next connection. **Audio buffer**: *Low latency* (100 ms), *Normal*
  (200 ms, the default) or *Smooth* (400 ms) — a longer buffer evens out a
  jittery Wi-Fi or mobile link for a little more delay. Low latency is for a
  wired or very steady link only; on ordinary Wi-Fi it stutters. If Normal
  stutters, try Smooth. Every 10 s with stutter the log gets an
  "audio underrun" line (count, buffer, longest packet gap, buffer mode),
  and once a minute a "performance" line (CPU, frames drawn). Below, the radio's
  8-band receive and transmit equalizer
  (volume is on the info panel's speaker).
- **TX** — in FM modes first **FM repeater**: **Offset** *Simplex*, *−*
  (transmit below) or *+* (above), the **Shift** (0.6, 1.6, 5.0 or 7.6 MHz,
  or type any) and **Tone**: *CTCSS* on transmit with its tone. The radio
  transmits on the shifted frequency and sends the tone; receive stays on
  the frequency shown. Then TUNE and the
  ATU (tune, bypass, memories), tune power, RF power, speech processor (PROC
  NOR/DX/DX+), **VOX** (on/off, level and delay) and **DEXP** (the downward
  expander, on/off and level) in voice modes — shown only when the radio's
  own microphone input is selected, as they do nothing with this device's
  microphone — the AM **carrier** level in
  AM/SAM/DSB, monitor (MON), **Mic level**, microphone choice (when the
  radio's mic input is not this device, **Use this device's microphone**
  sets it and the line beside it says why PTT waits), **DAX**
  (on: the radio sends the audio of a digital-mode program on a computer
  instead of the microphone; left on after FT8, the microphone goes
  nowhere), **PTT**
  (*Hold to talk* or *Tap on / off*) and the transmit filter (low and high
  cut). **Mic level** with this device's
  microphone is the app's own gain, −20 to +30 dB: the radio has no gain
  stage for its PC input, so its mic level does nothing to this
  microphone. Choose Mic for the left meter and set it so speech peaks
  near −10 dB: while the TX tab is open the microphone is open and the
  meter moves in receive too (nothing is sent before PTT; not while VOX is
  on). Phone microphones often need +10 to +20 dB; a limiter at
  −1 dBFS keeps the loudest peaks from clipping. With a microphone on the
  radio it is the radio's mic level (0–100), as SmartSDR's MIC.
  - **This device's microphone: processing before it is sent** (voice modes, with this
    device's microphone) — AetherSDR's client-side transmit chain, run on
    the phone or tablet before the audio is sent: **Gate** (mutes the
    background between words below the threshold), **EQ** (low cut
    100–300 Hz, bass and presence ±12 dB) and **Comp** (threshold, ratio
    2:1–8:1 and makeup; while you transmit, how much it is taking off shows
    beside the ratio). The radio's own 8-band transmit EQ is in Audio.
    Check the result with MON or a second receiver at low power first.

    ![Processing](manual/tx-processing.jpg)
  - **Voice keyer** (voice modes) — twelve voice macros F1–F12. Tap one to
    transmit it, tap again to stop; press and hold to record (from this
    device's microphone; recording does not transmit), rename or clear. On a
    radio with SmartSDR+ these are the radio's own DVK slots (up to 10 s,
    shared with other programs); otherwise the app keeps them on this
    device (up to 15 s) and plays them into the transmit audio as if
    spoken. Either way **Listen** plays a recording to you without
    transmitting, so you can check it before naming it. Features that need
    SmartSDR+ appear only when the radio reports the subscription.

| CW | View | Prof | Mem |
|---|---|---|---|
| ![CW](manual/tab-cw.jpg) | ![View](manual/tab-view.jpg) | ![Profiles](manual/tab-profiles.jpg) | ![Memory](manual/tab-memory.jpg) |

- **CW**
  - **Callsign** — the callsign `{CALL}` sends, the radio's own. Set or
    change it in [Radio Setup](#9-radio-setup) → Radio.
  - **Memories M1–M12** — tap to send (through the radio's CWX keyer at the
    set speed), press and hold to edit, or F1–F12 on a keyboard. `{CALL}`
    becomes your callsign. They are the app's own; the radio's stored CWX
    macros are left alone. What they send, and CW text below, plays as
    sidetone on this device too.
  - **CW text** — type and *Send*; with **Live** on, each letter goes as you
    type it (Return sends a space). *Stop*, or Esc, clears what is still
    queued.
  - **Key** — *Straight key* or *Paddles*; with paddles, **Iambic** A / B and
    **Swap ⇄**, and **Paddles on a keyboard** (see
    [KEY](#7-transmitting-ptt-key-and-tune)).
  - Speed, pitch, **Break-in** (QSK) and the **Decoder**. With Break-in off
    the key does not put the radio on air; the app says so when you key.
    The decoder has **Sens**
    (0–100, default 70): higher shows only the letters decoded with
    confidence, so band noise stays out. It reads hand-sent and keyer CW
    from 8 to 45 WPM. Decoded text shows
    here and along the top of the waterfall (the last two lines, newest at
    the bottom right); what you send shows there too, marked `[TX]`. The
    decoder keeps listening with the app in the background: `[away 20:55]`
    and `[back 21:12]` mark what came while you were away. A callsign the decoder hears after *DE*, or twice, is
    offered on the Log tab (*Heard in CW*).
- **View** — **Band plan** (the strip at the foot of the spectrum: *Off*,
  *ARRL (US)*, *IARU Region 1 / 2 / 3*, *RAC (Canada)* or *SSA
  (Sweden)*), **Spots** (shown or hidden) and the **DX cluster**,
  2D waterfall or 3D, waterfall levels, spectrum range, 3D floor, smoothing
  and gain.
  - **Spot filters** — which spots show: *All*, *CW*, *Phone* or *Digital*
    (by the spot's mode, else its comment or the band plan, as on the
    desktop), and *Any age* or only those from the last 5, 15 or 30
    minutes.
  - **DX cluster** — tap to connect, again to disconnect. Set the server and
    port (default dxc.nc7j.com 7300) and the callsign it logs in with (the
    radio's own when left empty); **Auto** connects whenever the radio
    does. Its spots go to the radio, so SmartSDR and every other program
    on it show them too; they expire after 30 minutes. The line under the
    button tells whether it is on: green *Connected to … as …* with the
    spots so far and the latest one's time (UTC), *Connecting…*, or red
    with the reason when it could not connect.
- **Prof** (profiles) — the radio's global, TX and mic profiles: load, save,
  delete, as in SmartSDR.
- **Mem** (memory) — the radio's memory channels: tap to tune, *Save* stores the
  current frequency under the name typed. **Press and hold** one to edit
  its name, frequency (MHz) and mode — *Current* takes the slice's
  frequency and mode — or to *Delete* it. In FM modes it also holds the
  repeater: *Simplex*, *−* or *+* with the shift (0.6, 1.6, 5.0, 7.6 MHz
  or typed) and the CTCSS tone; the list shows them (e.g. "−7.6 88.5"),
  and tapping the memory transmits on the shifted frequency with the tone.
  *Save* with an FM repeater set on the TX tab stores it too.

  ![Editing a memory](manual/memory-edit.jpg)

![Log tab](manual/tab-log.jpg)

- **Log** — quick QSO logging into a log on this device (an ADIF file,
  `oksasdr-log.adi`). Type the **callsign**, or tap the call of the spot
  you tuned to last. With the CW decoder on, **Heard in CW** offers the
  calls it heard (after *DE*, or twice; the newest first, up to three):
  a tap puts one in the field, handy when a park activator sends
  `CQ POTA DE` and the call. A card shows who it is: with a **QRZ.com** account
  (user and password at the bottom; *Save* signs in and says below whether
  it worked; their full records need QRZ.com's XML subscription) the name, QTH, grid, distance and bearing and whether the
  station uses LoTW, eQSL or paper cards; without one, the country,
  continent and CQ zone from the callsign's prefix. It also says when the
  call was **worked before** and when it would be a **dupe** on this band
  and mode. The reports start at 59 (599 in CW and data) and follow the
  mode until you type one. Frequency, band, mode, power, the UTC time the
  call was typed and your own callsign come from the radio; your grid
  comes from the radio's GPS, or from *My grid*. **Their park** and **My
  park** (POTA or WWFF with the reference) go into ADIF's SIG / MY_SIG
  fields; your own park stays for every contact while you activate.
  *Sent* and *Rcvd* hold the reports and *Comment* a note for the
  contact. **Log CALL** (the button names the call) writes the contact;
  the newest ten are listed. **Share the log
  (ADIF)** opens the share sheet with the whole log as an `.adi` file: mail
  it, save it to Files or Drive, AirDrop it, or open it in your logbook
  program or TQSL for LoTW. **Import contacts (ADIF)** adds the contacts of
  an ADIF file (from another logbook, or another device's OksaSDR log);
  those already in the log (same call, date, time, band and mode) are
  skipped. A big logbook (tens of thousands of contacts, tested with
  120 000) imports in a few seconds while the display goes on, and the log
  is read in the background at start (worked-before colours follow when
  it is in); the line under the button says
  *Importing…* and then how many came in. The QRZ.com password is kept in this device's secure store.
  **Wavelog / Cloudlog**: with your logbook's address (as its API page
  shows it, e.g. `https://log.example.com/index.php`), a read & write
  **API key** (Wavelog: *API* › *Legacy API Keys (API v1)*, starting
  `wl…`; Cloudlog: *API Keys*) and the **station ID** (the number of your
  station location there), *Save* checks the key and says whether the
  logbook accepted it; from then on every QSO you log, by hand or from FT8 /
  FT4, also goes to the logbook. QSOs that cannot be sent (offline, server
  away) wait on the device and go with the next QSO or start; the line
  under the fields says how many wait. Clear the address to stop.
  **Where the log is** (shown on the Log tab): on an iPhone or iPad in
  Files › On My iPhone / iPad › OksaSDR › `oksasdr-log.adi`, from where it
  can be moved to iCloud Drive or opened in a logbook; on Android a copy is
  kept in Files › Documents › OksaSDR; on a computer in
  Documents/OksaSDR. A log from an earlier build moves there by itself.
- **FTx** — FT8 and FT4 in the app itself, no computer, DAX or WSJT-X
  needed.

  | FTx tab | Decodes in WSJT-X colours |
  |---|---|
  | ![FTx](manual/tab-digi.jpg) | ![Decodes](manual/digi-decodes.jpg) |

  **Decode** turns decoding on (*FT8* or *FT4*); a band button makes the
  active slice the TX slice, tunes it to the mode's dial frequency in DIGU,
  starts decoding and zooms the display to the 4 kHz audio band (again with
  *Zoom*). The bar shows where in the 15 s (FT4: 7.5 s) slot you are.

  **Every slice in DIGU / DIGL decodes at once** (say 40 m on slice A and
  20 m on slice B), each on a DAX channel of its own so the other slices'
  audio stays out: a slice without one, or one sharing its channel with
  another slice, gets a free channel while it decodes and its old one back
  after. The decodes come at the end of each slot, newest first: slice
  letter, UTC, SNR, DT, audio frequency and the message.

  The colours are WSJT-X's: green a CQ, red your call, yellow your own
  transmission, magenta a new DXCC (light pink: new on this band), amber a
  new CQ zone (light amber: on this band), red-pink a new continent (light
  pink-red: on this band), cyan a call never worked (light cyan: not on
  this band; WSJT-X's New Call and New Call on Band); a CQ
  from a station already in the log on this band and mode is grey. "New"
  is counted from the Log tab's log, so import your logbook there (ADIF)
  to see what you still need. **Show** filters the list: **All**, **CQ**
  (CQ calls only), **CQ POTA** (park activators calling `CQ POTA`; a
  contact with one goes into the log with SIG POTA, the park itself is not
  in the message), **New** (a new continent, CQ zone or DXCC, overall or on
  this band), **New DXCC** (a DXCC not yet in the log, overall or on
  this band) or **New call** (a call not yet in the log, or not on this
  band); your call, your own transmissions and the station you are
  working always show. The line above the list says how many of the last
  decodes the filter shows.

  **Tap a decode** to answer that station: the row flashes yellow, TX,
  the active slice and the display move to the slice it was heard on, and
  the app starts calling.
  **CQ** calls CQ on the **TX slice** (the one with the red TX badge), in
  the slot set by **CQ slot**: *Auto* (the next one), *Even* (FT8: 0 and
  30 s, FT4: 0, 15, 30, 45 s) or *Odd* (15 and 45 s; FT4: 7.5, 22.5 …).
  **Keep CQ** goes back to CQ after every QSO your CQ brought (and after a
  station that answered goes quiet), until **Halt TX**; a CQ nobody
  answers still stops after its tries. Timing and transmitting follow the TX slice: move the TX
  badge, press a band button with another slice active, or tap inside
  another DIGU / DIGL slice's passband to move it; a QSO under way stops
  when TX moves, and adding slices does not take TX away.

  While a QSO or CQ runs, its state is a box under TX level: amber while
  waiting (*Calling K1ABC*, *Next (odd): …*), red while transmitting. The
  same state also shows at the top of the display, where it stays in view
  however far the decode list is scrolled. On
  the display the red band is your TX tone and a green **RX** band shows
  where the station you work is heard, as WSJT-X's markers (below, FT8
  decoding on 20 m: the 4 kHz FT8 view, the TX and RX bands, and Controls
  in PTT's place).

  ![FT8 decoding on the display](manual/ft8-running.jpg)

  RX is
  always shown: after a QSO it stays on the last station worked; before
  the first one, and while you call CQ, it sits on your own TX tone; both labels sit on solid grounds over the
  waterfall.

  **Report to PSK Reporter** (off by default) sends the stations this
  device decodes, with their locators, frequency and SNR, to
  pskreporter.info under your callsign and grid (set the grid on the Log
  tab if the radio has no GPS), as WSJT-X does: each station at most once
  per band in five minutes, sent every five minutes and when decoding stops.
  The line under it counts what was reported.

  A QSO between two **nonstandard calls** (yours with a four-letter suffix,
  and theirs with a / suffix such as /QRP) has no FT8 message for a report:
  the app makes it with the calls and RR73 only, and logs it without RST.

  If the decodes' DT is off by more than 0.8 s on average, a red notice
  says this device's clock looks off: FT8 needs it within about a second,
  so set Date & Time to automatic.

  The app then runs the QSO as WSJT-X does (report, R-report, RR73 or RRR,
  73), stops after six tries without the reply it waits for, and writes the
  contact to the Log tab's log (MODE FT8, or MFSK / FT4) with the
  frequency of the slice it was made on. **Halt TX** stops at once. PTT is
  hidden while FT8 / FT4 decodes (the app keys the radio itself), and the
  display gets the room. Switching the FT8 slice to a voice or CW mode
  (by hand, or with a band button on a band the radio remembers in LSB /
  USB) ends decoding: PTT comes back and the display returns from the
  4 kHz FT8 view to the width it had before.

  **TX … Hz** sets the transmit tone (200–3000 Hz, default 1200; − / + step
  1 Hz and repeat while held). The red band on the display is the tone:
  **drag it** to any frequency, or **tap** the waterfall near the dial to
  put it there (while decoding, a tap there sets the tone instead of
  retuning; the passband shading is left out in FT8 / FT4, the filter edges
  stay). A callsign the standard messages cannot carry (such as OH6OKSA,
  with a four-letter suffix) is sent as WSJT-X sends it: in full in CQ and
  in RRR / RR73 / 73, as <CALL> in the messages with a report. The transmit
  audio goes to the radio past the TX processing (no gate, EQ or
  compression), and the radio's speech processor (PROC) is switched off
  for each FT8 / FT4 transmission and back on right after it, so a PROC
  left on cannot distort the tones. **TX level** (0 to −20 dB, default 0 dB, as WSJT-X's
  Pwr) sets how hard that audio drives the radio: at 0 dB it is fully
  driven and the power out follows the TX tab's RF power; lower it to back
  off. Under it, **Last TX** shows the last transmission's peak power and
  SWR beside the band's RF power: well below it means the drive (TX level)
  or the SWR; equal means the band's RF power is the limit. **Free** moves
  the TX tone to the clearest spot between 200 and 2800 Hz among the
  stations heard in the last minute on the TX slice (in the CQ slot's
  period when Even or Odd is chosen); the status line says where. Decoding copes with audio
  that arrives in bursts (SmartLink, a VPN over a mobile link).

- **FreeDV** — RADE digital voice and FreeDV Reporter.

  ![FreeDV tab](manual/tab-freedv.jpg)

  **RADE digital voice** (FreeDV's Radio Autoencoder, in the app itself, no
  FreeDV waveform needed): *RADE* puts the slice in DIGU (DIGL where the
  band's voice mode is LSB) with a 750–2250 Hz filter, mutes its ordinary
  audio and plays the decoded speech instead; the line beside it shows
  *Synced* and the SNR when a RADE signal is heard, and the callsign each
  station sends at the end of its over. Talk with PTT as usual: this
  device's microphone goes through the RADE encoder to the radio's DAX
  transmit (turned on for RADE, off again after), and the end-of-over frame
  with your callsign plays out before the carrier drops. While RADE is on,
  the filter chooser's widths centre on RADE's 1500 Hz (the chooser says
  so), not on the radio's DIGU / DIGL offset, so a narrower or wider filter
  stays on the signal. *RADE* again, or another mode, ends it and restores
  the mode and filter (unmuted; when the mode before RADE is not known, the
  band's voice mode with a 2.7 kHz filter); a band button, a frequency on
  another band, an FTx band button, *Decode* and *Disconnect* end it first
  too, so the radio remembers the band as it was before RADE (not muted).
  If the app was closed or the link lost while RADE was on, the next
  connection unmutes the slice RADE had muted. When a band comes back muted
  anyway (the radio remembers each band's slice), the app says so: tap the
  speaker on the info panel.

  ![RADE on 14.236 MHz: DIGU with RADE's filter, 750–2250 Hz above the slice line](manual/rade-on.jpg)

  **Report to FreeDV Reporter** (off by default) puts this station on
  [qso.freedv.org](https://qso.freedv.org), as FreeDV does, while RADE is
  on: your callsign and grid (the Log tab's *My grid* or the radio's GPS),
  the frequency, when you transmit, and the stations RADE hears with their
  SNR. The line under it says whether it reports (green: *Reporting as …*)
  or what it needs; the text field sets the short message shown there.

  **Stations** lists who is on FreeDV Reporter now, on this band or on all
  bands, the stations on the air first: those transmitting now (*TX now*
  in red), then the most recently heard (*TX 3 min ago*), then the others
  by frequency. Each shows its callsign, grid and distance, frequency,
  mode, whom it hears and the message it shows. Your own station is
  outlined. A tap tunes there; for a RADE station (mode RADEV1) RADE comes
  on too, and for an SSB station (LSB / USB) RADE goes off and the slice
  takes the station's sideband, so you hear it. The list is fetched only while the tab
  is open.

## 9. Radio Setup

**Setup** in the top row (on an iPhone: **⋯** → *Radio Setup*) holds the
radio's own settings — the ones a radio needs on first use, as in SmartSDR's
and the desktop AetherSDR's Radio Setup. Every change is written to the
radio.

![Radio Setup](manual/setup.jpg)

- **Radio** — callsign (what `{CALL}` sends), the radio's name in radio
  lists, *Remote on* (SmartLink may power the radio on), muting the radio's
  own audio while remote, **Connected stations** (every program on the radio,
  multiFLEX; *Disconnect* ends another station's session after asking, which
  frees its slices), and the model, serial, firmware, region and address.
- **Transmit** — maximum power, TX delay, transmit timeout (0 = off) and the
  TUNE signal (single or two tone).
- **Phone/CW** — microphone bias and boost (the radio's own microphone
  inputs; this device's microphone gain is the TX tab's Mic level); the CW
  keyer's break-in delay,
  iambic mode (off, A, B) and swapped paddles.
- **Audio** — the radio's line out and headphone levels and muting.
- **Network** — the network MTU: the largest packet the radio sends the app
  (1450 by default, as in SmartSDR and the desktop). Lower it if the
  waterfall or audio break up over a VPN or tunnel; over Tailscale at most
  1200 is used.
- **XVTR** — the radio's transverters, as the desktop's XVTR tab: *Add
  transverter* creates one; for each, its name (shown on the band buttons),
  RF frequency (the band, MHz), IF frequency (the radio's, MHz; LO = RF −
  IF is shown), LO error (MHz, for the converter's offset), RX gain (dB),
  max power (dBm of drive to the transverter at its IF, kept within what the
  radio allows) and *Receive only*; *Remove* deletes it. The radio says when
  a transverter is valid.

**A new radio.** When the app connects to a radio that has no callsign yet —
a new or reset radio — it offers *Set up* once, a few seconds after
connecting. *Later* skips it; Setup stays in the top row either way. A radio
that has a callsign never shows the offer.

## 10. Notices and problems

Notices and refusals pop up in the middle of the display; tap one, or wait
6 s, to close it. A tap elsewhere (PTT, tuning) never closes it by accident.

![No slice notice](manual/noslice.jpg)

| You see | Why | What to do |
|---|---|---|
| **No slice** (frequency 0.000.000) | The radio did not give the app a slice. The app first retries once on a fresh panadapter; if it still fails, the radio's slices may all be in use by other programs | **Stations…** shows them; close a slice there or disconnect it, then **Try again** |
| *No slice transmits* | No slice has transmit | Press and hold a slice → **Make TX slice** |
| *Interlock / did not accept the transmit request* | The radio refused to transmit; its reason is shown | Check TX antenna, band, other clients |
| A band button opens another band | The radio's band stack (in its global profile) holds that frequency | Tune the band where you want it and save the profile |
| *The radio has no callsign for {CALL}* | No callsign in the radio | Setup → Radio → Callsign |
| *The TX slice is in DIGU: PTT is for voice …* | PTT pressed with the transmit slice in DIGU / DIGL | Switch it to USB / LSB to talk; FT8 / FT4 run from the FTx tab, RADE from the FreeDV tab |
| *Break-in is off …* | CW keyed with Break-in off: the key does not put the radio on air | Controls → CW → Break-in |
| *This slice is muted on this band …* | The radio remembers the band's slice muted | Tap the speaker on the info panel |
| *Microphone access is off …* | The device does not let the app use the microphone | Allow it in the device's settings (Settings › OksaSDR › Microphone) |
| *No free slice for the transmit side of a split* | All the radio's slices are in use | Close a slice (press and hold its chip, or Stations…) |
| *Set the radio's callsign first* / *The DX cluster needs a callsign* | FT8 / FT4, or the DX cluster, needs a callsign | Setup → Radio → Callsign (the DX cluster can also take one under Spots) |
| *The radio takes transmit audio from its … input* | PTT would send the radio's own microphone | Controls → TX → **Use this device's microphone** |
| *The 3D view was too slow …* / *The app stopped while the 3D view was on …* | The app went back to the 2D waterfall | View → 3D to try again |

## 11. Keyboard and Apple Pencil

On a **Mac** (the iPad app, through TestFlight) the Mac's keyboard does not
reach the app's own text fields, so clicking one opens a small text box
(Apple's own) with the field's text: type there and press Return or OK to
put the text in the field, as if typed there and Return pressed; Esc or
Cancel leaves the field as it was. The iPad's GameController keyboard
reading is not used on a Mac.

With a Magic Keyboard, trackpad or any hardware keyboard (also a
Bluetooth keyboard on a phone, or Android's DeX):

| Key | Action |
|---|---|
| ← / → | Tune one step |
| ↑ / ↓ | Tune ten steps |
| Shift + arrows | Ten times as far |
| Space | PTT (hold) |
| + / − | Zoom |
| F | Frequency entry |
| F1–F12 | CW memories (CW modes) or voice macros (voice modes) |
| Esc | Stop CW text or a voice macro |
| Scroll | Tune; ⌘- or Ctrl-scroll zooms |

**Apple Pencil** drags tune ten times finer, for zero-beating.

## 12. Reporting a problem

Open **Log** right after the problem (on an iPhone behind **⋯**), tap
**Copy** (or **Share**) and send the text to oh6oksa@gmail.com together with
your device model and what you were doing. The log header shows the app
version and build. **Clear** empties the log, so the next copy starts from
a known point. **Developer link** (with a PC address) is only for the
developer's own devices: leave it off.


## 13. Android

OksaSDR for Android is in beta. It is the same app as on the iPhone and
iPad, built from the same code; this section covers what is particular to
Android.

| Inner screen | Controls open | Outer screen |
|---|---|---|
| ![OksaSDR on Android](android-foldable.jpg) | ![Controls drawer on Android](android-foldable-controls.jpg) | ![OksaSDR on an Android phone screen](android-phone.jpg) |

**Installing.** Download the newest APK from
[the latest release](https://github.com/OH6OKSA/OksaSDR/releases/latest) on the phone, open
it, and allow installing from your browser or file manager when Android
asks. A newer beta installs over the old one and keeps your settings.
Android 9 or newer, 64-bit ARM (practically every phone and tablet from the
last years).

**Permissions, asked once.**
- *Notifications*, *microphone* and *battery optimization* — the first
  time you connect. The microphone is used only while PTT is held (or a
  voice macro is recorded); if you refused it, PTT asks again, and when
  Android no longer asks, turn it on in Settings → Apps → OksaSDR →
  Permissions → Microphone.
- Allow notifications and battery: while connected, OksaSDR runs a service with a notification
  (*OksaSDR — Connected to the radio*) so receive audio and the connection
  keep going when another app is in front or the screen is off. Some phones
  (OnePlus, Xiaomi, Huawei) also have their own setting: Settings → Apps →
  OksaSDR → Battery → allow background activity.

**Bluetooth headsets.** With a headset connected when you connect to the
radio, receive audio and the microphone go through it (the hands-free link,
as on the iPhone). Without one, the phone's speaker and microphone are used.

**Finding the radio.** On the radio's own network, *On this network* lists
it. Over SmartLink or a VPN, use those sections instead (the radio's
broadcasts do not cross a VPN).

**Log.** *Log → Share* opens Android's share sheet (Gmail, Messages, …).
