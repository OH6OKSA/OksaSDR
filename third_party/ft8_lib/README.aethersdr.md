# ft8_lib (vendored)

FT8 / FT4 encoder and decoder by Kārlis Goba, MIT licence (LICENSE).
Upstream: https://github.com/kgoba/ft8_lib, commit
9fec6ca39886edbf96f4f5e71edc76da5074e871 (2025-08-24).

Copied: `ft8/`, `fft/` (KISS FFT) and `common/monitor.{c,h}`,
`common/common.h`. Left out: the demos, tests and the WAV / audio helpers.

Changed in OksaSDR (2026-11-09, roadmap 51): `ft8/message.{c,h}` decode the
types ft8_lib left as "not handled": 0.1 DXpedition, 0.3 / 0.4 ARRL Field
Day, 3 ARRL RTTY Roundup and 5 EU VHF contest, after WSJT-X 2.7's unpack77
(lib/77bit/packjt77.f90, its bit table lib/77bit/77bit.txt and its ARRL
section and state / province tables; WSJT-X is GPLv3, as OksaSDR), and
FTX_MAX_MESSAGE_LENGTH is 56 for the DXpedition message. Tested with
`aether-mobile --ft8-test selftest` (each type packed bit by bit) and
`--ft8-wav` on WSJT-X's sample recording 181201_180245.wav (the 2018 FT8
Roundup).

The KISS FFT here and the one in RADE (third_party/radae) export the same
symbol names, so OksaSDR builds this copy with its symbols renamed
(`ftx_` prefix, see mobile/CMakeLists.txt).
