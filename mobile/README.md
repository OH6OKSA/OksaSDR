# OksaSDR: the mobile client (`mobile/`)

OksaSDR is a touch client for iPhone, iPad, Mac (Apple silicon) and Android
that links `libaethercore` in-process and talks to the radio directly, with
no PC, `aetherd` or SmartSDR in between. It is configured only when
`AETHER_MOBILE` is ON (see `BUILD-OPTIONS.md`).

Status: beta, on TestFlight (iPhone, iPad, Mac) and as Android betas in
GitHub Releases. What it does is in the [README](../README.md) and the
[user manual](../docs/mobile/MANUAL.md); what changed and what comes next is
in the [roadmap](ROADMAP.md). Privacy policy: [PRIVACY.md](PRIVACY.md).

The code: `src/MobileEngine*.cpp` composes the engine for the QML pages
(radio, slices, audio, transmit, FT8 / FT4, RADE, the log, FreeDV
Reporter), `src/SpectrumItem` and the 3D surface draw the display, and
`qml/` holds the tablet and phone pages, the controls drawer and its tabs.
`ios/` and `android/` carry the platform code and packaging.

## Develop on Linux or macOS

```bash
scripts/setup/setup-qt.sh            # pinned Qt, includes Qt Quick
cmake -B build-mobile -DAETHER_MOBILE=ON
cmake --build build-mobile --target aether-mobile --parallel
build-mobile/mobile/aether-mobile --demo
```

The `--demo` option connects to the simulator as soon as it is listed;
`--connect IP` connects to a real radio. Add `--screenshot out.png` to save
the window after a delay and quit, which also works with
`QT_QPA_PLATFORM=offscreen` (the 3D view needs a GPU and stays blank
there). `--help` lists the development options (layouts, popups, drawer
tabs, receive-only tests and the transmit tests, which key the radio).

## README screenshots

`mobile/tools/update-screenshots.sh [RADIO_IP]` builds the app and renders
the README's iPad mini portrait and iPhone portrait and landscape shots
(`docs/mobile/*.jpg`), plus the LSB passband from the demo radio;
`mobile/tools/update-manual-screenshots.sh [RADIO_IP]` renders the
manual's (`docs/mobile/manual/*.jpg`). The iPad landscape and Android
pictures are taken on the devices (the developer link below). Run them
with every user-interface change and commit the images with it.

## Keeping GitHub up to date

What GitHub shows (README, manual, tester guide, roadmap, TestFlight notes,
Android releases) is checked and partly published automatically:

- `mobile/tools/update-docs.sh RADIO_IP` refreshes the README and manual
  screenshots against the radio (receive only), then runs the check below.
- `mobile/tools/docs-check.py` checks the unpushed commits: an app change
  needs new `mobile/TESTFLIGHT_WHATS_NEW.txt` and a `mobile/ROADMAP.md`
  update; a UI change (`mobile/qml`) needs `docs/mobile/MANUAL.md` and new
  screenshots. The notes must fit 4000 characters in English; every link,
  image and anchor in the docs must resolve; no old repository names,
  phone numbers or serial numbers. A rule that does not apply is waived
  with a commit trailer that says why, e.g. `Manual: unchanged (internal
  fix)` (also `Screenshots:`, `Roadmap:`, `Notes:`).
- The pre-push hook runs the check for mobile/** and android/** branches.
  Turn it on once per clone: `git config core.hooksPath mobile/tools/githooks`.
- `.github/workflows/mobile-docs.yml` runs the same check on GitHub for
  every push; a red mark there names the stale doc.
- Android releases: when `mobile/android/release.env` names a tag that is not
  on GitHub yet, the Android build of `mobile/oksasdr` publishes the
  release-signed APK as that release, marked Latest (so `releases/latest`
  is always the newest beta), with `mobile/android/RELEASE_NOTES.md`.

## iOS

`.github/workflows/mobile-ios.yml` builds the engine and the app for iOS arm64
(iPadOS / iOS 18+, also run on Macs with Apple silicon) on a GitHub-hosted
macOS runner (the xcode-27 image). No Mac is needed. A push to a `mobile/**`
branch that changes the app (engine, `mobile/src`, `mobile/qml`,
`mobile/ios`, shaders) builds it; on `mobile/oksasdr` the build is signed
and uploaded to TestFlight, and its build number is the workflow's run
number. Docs, screenshots and tools do not start a build. Without the
signing secrets the workflow uploads `aether-mobile-unsigned.ipa` as a run
artifact (sideload it with your own Apple ID if you like).

### TestFlight for your own fork

1. **Bundle ID.** In developer.apple.com → Certificates, IDs & Profiles →
   Identifiers, register an App ID such as `fi.example.aethersdr`.
2. **App record.** In App Store Connect → Apps → "+" → New App, choose
   iOS, the bundle ID from step 1 and any name and SKU.
3. **API key.** In App Store Connect → Users and Access → Integrations →
   App Store Connect API, create a key with the *App Manager* role. Note
   the Issuer ID and Key ID, and download the `.p8` file. Apple allows the
   download only once.
4. **Team ID.** Copy it from developer.apple.com → Membership details.
5. **GitHub.** In the fork, open Settings → Secrets and variables → Actions:
   - Secrets: `APPLE_TEAM_ID`, `APPSTORE_API_KEY_ID`,
     `APPSTORE_API_ISSUER_ID`, and `APPSTORE_API_KEY_P8` (paste the whole
     `.p8` file, including the BEGIN/END lines).
   - Variable: `AETHER_MOBILE_BUNDLE_ID` set to the bundle ID from step 1.

The workflow uploads only from the branch it names (`mobile/oksasdr`; change
it for your fork). After Apple finishes processing a build (usually 5–30
minutes), add testers in App Store Connect → TestFlight. Testers install
the TestFlight app and accept the invitation; each enters their own radio's
IP address in the app or signs in to SmartLink. LAN discovery on iOS needs
Apple's multicast entitlement; without it, connect by IP address.

## Android

`.github/workflows/mobile-android.yml` builds the arm64 APK from the same
code (Qt for Android, FFTW cross-built by `tools/build-fftw-android.sh`) on
`mobile/**` and `android/**` branches when the app changes. With the
secrets `ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASSWORD`,
`ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD` the APK is release-signed;
on `mobile/oksasdr`, when `android/release.env` names a tag that is not on
GitHub yet, it is published as that release (see Keeping GitHub up to
date). The app shows the beta's number in brackets (`android-beta-N` shows
N).

## Developer link

For on-device debugging the app can stream its log to a developer PC and
take screenshots on request. It is off by default; turn it on in the app
(Log page → Developer link, enter the PC's address). On the PC run

    mobile/tools/devlink.py serve --out /some/dir
    mobile/tools/devlink.py send '{"cmd":"screenshot"}'

The app connects out to port 47100 (allow it in the PC's firewall). It
accepts only receive and display commands (screenshot, state, set on an
allow-list, tune, mode, band, zoom, drawer); nothing on the link can key
the transmitter.
