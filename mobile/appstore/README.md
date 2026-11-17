# OksaSDR in the App Store

`mobile/tools/appstore_sync.py` puts these texts into the editable App Store
version in App Store Connect (without `--apply` it shows the changes only).

The App Store listing's texts (en-US) and the answers App Store Connect asks
for, kept here so every release uses the same ones (roadmap 98). The
TestFlight texts stay in `mobile/TESTFLIGHT_WHATS_NEW.txt` and
`mobile/TESTFLIGHT_DESCRIPTION.txt`.

| File | App Store Connect field | Limit |
|---|---|---|
| `en-US/name.txt` | Name | 30 |
| `en-US/subtitle.txt` | Subtitle | 30 |
| `en-US/promotional_text.txt` | Promotional Text (changes without a review) | 170 |
| `en-US/description.txt` | Description | 4000 |
| `en-US/keywords.txt` | Keywords (comma separated, no spaces needed) | 100 |
| `en-US/support_url.txt` | Support URL | |
| `en-US/marketing_url.txt` | Marketing URL | |
| `en-US/privacy_url.txt` | Privacy Policy URL | |
| `en-US/review_notes.txt` | App Review Information › Notes | 4000 |

Rules kept in the texts: no other platform's name (guideline 2.3.10: no
Android here), no third-party trademarks in the name, subtitle or keywords
(2.3.7; FlexRadio's names only in the description, as compatibility, with the
trademark line), no "beta" or "TestFlight", and a public privacy policy
(`mobile/PRIVACY.md`).

## Screenshots

`mobile/tools/appstore-screenshots.sh RADIO_IP --build N` renders them
against the real radio (receive only; slice A put back after) with the
submitted build's number in the status line, into `screenshots/` (not
committed: made again for each submission):

| 6.9" iPhone, 1320 x 2868 | 13" iPad, 2752 x 2064 |
|---|---|
| 1 the main screen on 40 m FT8 | 1 the main screen, DSP and Audio pinned |
| 2 FT8 decoding, the FTx tab full screen | 2 FT8 decoding, the FTx list pinned |
| 3 the Log tab with W1AW's lookup | 3 the CW tab |
| 4 Big buttons | 4 Radio Setup › Transmit |
| 5 Radio Setup › Transmit | |

Kept out of them: Radio Setup's Radio section (it names the radio and the
computer), the iPad's Log tab (it shows the render computer's log file path)
and the 3D view (the render tool draws no 3D without a GPU). The Log shot
types with the transmit lock on (A and T would tune on the page).

## App Privacy (App Store Connect › App Privacy)

Answer: **Data Not Collected.** The app has no analytics, advertising,
tracking or crash-reporting SDKs and nothing reaches the developer
(`mobile/PRIVACY.md`). What leaves the device goes to the radio or to a
service the user turns on and points the app to (SmartLink under FlexRadio's
terms, QRZ.com, a DX cluster, POTA, PSK Reporter, FreeDV Reporter, the user's
own logbook): the app is that service's client, it is not the developer's
collection. A log sent with **Send to the developer** is optional,
infrequent and started by the user each time (Apple's optional-disclosure
case).

## Age rating (App Store Connect › Age Rating)

Every content question: **None**. Unrestricted web access: **No** (the
manual opens in the system browser). Gambling, contests: **No**. The DX
cluster and FT8 / FT4 show short messages that other radio amateurs send on
the air; if the questionnaire asks about communication between users, answer
**Yes** and let the rating follow.

## Other answers

- Export compliance: `ITSAppUsesNonExemptEncryption` is false (HTTPS / TLS
  only); no documents needed.
- Content rights: the app accesses third-party content (SmartLink, QRZ.com,
  DX clusters, PSK Reporter, POTA): **Yes**, with the rights to use it.
- Price: free; availability: every country.
- EU Digital Services Act: **not a trader** (a free app from a private
  person). A trader's address, phone and e-mail would be shown in the store.
- Apple silicon Macs: available (the iPad app), as in TestFlight.

## Open before the first submission

1. The GPL question (roadmap 97 b): the AetherSDR maintainers' consent first,
   or the user's decision to go without it.
2. The review video: the demo video v5 (`~/Videos/oksasdr-tour-v5-720p.mp4`)
   goes to Apple only, as an App Review attachment
   (`mobile/tools/appstore_sync.py --apply --attach FILE`), not to a public link
   (the user, 2026-11-17).
3. The review contact (name, phone, e-mail) typed in App Store Connect by the
   user; it is not kept here, the repository being public.
4. The DSA status, the App Privacy answers and the age rating set in App
   Store Connect by the user (the API does not cover App Privacy).
