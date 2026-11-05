# Symbol fonts (Android)

Subsets of two Noto fonts, registered by `mobile/src/main.cpp` as fallback
fonts on Android, whose own Noto Sans Symbols lacks symbols the UI draws
(✎ ✕ ⌥ ⇄ …):

- `NotoSansSymbols2-Subset.ttf`: Noto Sans Symbols 2, U+2300–23FF,
  U+25A0–25FF, U+2600–26FF, U+2700–27BF, U+2B00–2BFF.
  Copyright 2022 The Noto Project Authors (https://github.com/notofonts/symbols).
- `NotoSansMath-Subset.ttf`: Noto Sans Math, U+2190–21FF, U+2200–22FF.
  Copyright 2022 The Noto Project Authors (https://github.com/notofonts/math).

Both under the SIL Open Font License 1.1 (`OFL.txt`). Made with fontTools:

    pyftsubset NotoSansSymbols2-Regular.ttf --layout-features='*' \
        --unicodes="U+2300-23FF,U+25A0-25FF,U+2600-26FF,U+2700-27BF,U+2B00-2BFF" \
        --output-file=NotoSansSymbols2-Subset.ttf
    pyftsubset NotoSansMath-Regular.ttf --layout-features='*' \
        --unicodes="U+2190-21FF,U+2200-22FF" --output-file=NotoSansMath-Subset.ttf
