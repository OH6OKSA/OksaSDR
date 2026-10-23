# ft8_lib (vendored)

FT8 / FT4 encoder and decoder by Kārlis Goba, MIT licence (LICENSE).
Upstream: https://github.com/kgoba/ft8_lib, commit
9fec6ca39886edbf96f4f5e71edc76da5074e871 (2025-08-24).

Copied unchanged: `ft8/`, `fft/` (KISS FFT) and `common/monitor.{c,h}`,
`common/common.h`. Left out: the demos, tests and the WAV / audio helpers.

The KISS FFT here and the one in RADE (third_party/radae) export the same
symbol names, so OksaSDR builds this copy with its symbols renamed
(`ftx_` prefix, see mobile/CMakeLists.txt).
