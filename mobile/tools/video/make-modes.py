#!/usr/bin/env python3
"""Fill modes.tmpl with the frequencies of the moment (from the DX cluster's spots).

    make-modes.py OUT.plan --ssb 14.252 "note" --cw 14.025 "note" \
        --dv 7.177 "note"   (the --digi block: no RTTY / PSK on the bands this time)
"""
import sys
from pathlib import Path

args = sys.argv[1:]
out = Path(args.pop(0))
vals = {}
while args:
    key = args.pop(0)
    if key == "--ssb":
        vals["SSB_MHZ"], vals["SSB_NOTE"] = args.pop(0), args.pop(0)
    elif key == "--cw":
        vals["CW_MHZ"], vals["CW_NOTE"] = args.pop(0), args.pop(0)
    elif key == "--digi":
        mode, mhz, note = args.pop(0), args.pop(0), args.pop(0)
        vals["DIGI_MODE"], vals["DIGI_NOTE"] = mode, note
        if mode == "RTTY":
            vals["DIGI_SETUP"] = f"freq {mhz}\nwait 1000\ntaptext CW\nwait 2000\ntaptext RTTY ~510,277"
        else:  # PSK31 / PSK63: the Digi section's text mode on DIGU
            vals["DIGI_SETUP"] = f"textmode {mode}\nfreq {mhz}\nmode DIGU"
    elif key == "--dv":
        vals["DV_MHZ"], vals["DV_NOTE"] = args.pop(0), args.pop(0)
    else:
        sys.exit(__doc__)
text = (Path(__file__).with_name("modes.tmpl")).read_text()
for k, v in vals.items():
    text = text.replace("{" + k + "}", v)
if "{" in text.replace("caption", ""):
    missing = sorted({p.split("}")[0] for p in text.split("{")[1:]})
    sys.exit(f"unfilled: {missing}")
out.write_text(text)
print(out)
