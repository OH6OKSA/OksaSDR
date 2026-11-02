#pragma once

#include <QString>

namespace AetherSDR {

// G3PLX's Varicode for PSK31 / PSK63: each ASCII character a code of ones
// and zeros that starts and ends with a one and holds no two zeros in a
// row, so "00" ends a character (the common letters shortest: e "11").
namespace PskVaricode {

// The code of an ASCII character (0-127) as '1' / '0' characters, empty for
// anything else.
QString code(QChar ch);

// The character of a code read as bits, the first bit the most significant
// (e "11" = 3), or a null QChar when no character has it.
QChar character(unsigned bits);

} // namespace PskVaricode

} // namespace AetherSDR
