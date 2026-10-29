#!/usr/bin/env python3
"""Every window size in one run, on one connection to the radio.

    mobile/tools/render-sweep.py OUT_DIR [--radio IP] [--font PX] [--sizes WxH,...]
                                 [--panels PLAN --panel-sizes WxH,...]

One app run connects once and resizes its window through the sizes (the
shot plan's `size`), where one run per size took a connection, a filled
waterfall and a 20 s pause for the radio's GUI client slot each: about 25
minutes for 35 sizes, now a few. For each size it logs the layout and takes
a picture in DIGU, then again in USB (more DSP flags), and puts DIGU back.
The layout lines are split into OUT_DIR/WxH.log, as one run per size wrote
them, so mobile/tools/layout-diff.py compares two sweeps as before; the
pictures are OUT_DIR/WxH-digu.png and WxH-usb.png.

--panels PLAN runs that shot plan (the panel audit: popups, drawer tabs)
at each of --panel-sizes in the same run, its pictures named WxH-NAME.png;
its first `wait` (the display filling) is left out after the first size.

A resized window is laid out as a phone turning or a foldable opening; a
few sizes are worth a fresh start too (one run per size) before a release.
Needs a configured build in build-mobile/. Receive only: nothing is keyed.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

# The checkout this script is in (its build-mobile/ runs); --root for another.
ROOT = Path(__file__).resolve().parents[2]
SIZES = ("390x844 375x667 440x956 412x915 466x678 626x890 844x390 667x375 956x440 915x412 678x466 "
         "698x668 751x616 683x735 533x505 1366x1024 1024x1366 1180x820 820x1180 1440x900 1440x835 "
         "1470x890 1512x915 1710x1045 1728x1050 1920x1015 2560x1375").split()
LAYOUT = re.compile(r"shot-plan: layout window=(\d+x\d+)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--radio", default="192.168.0.40")
    ap.add_argument("--font", default="17")
    ap.add_argument("--sizes", default=",".join(SIZES))
    ap.add_argument("--panels", help="a shot plan to run at each panel size")
    ap.add_argument("--panel-sizes", default="390x844,667x375,1366x1024")
    ap.add_argument("--root", default=str(ROOT), help="the checkout whose build-mobile/ to run")
    args = ap.parse_args()
    root = Path(args.root)
    app = root / "build-mobile/mobile/aether-mobile"
    if not app.exists():
        print(f"no app at {app}")
        return 2

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    sizes = [s for s in args.sizes.split(",") if s]
    plan = ["wait 9000"]
    for state, mode in (("digu", None), ("usb", "USB")):
        if mode:
            plan += [f"mode {mode}", "wait 2500"]
        for size in sizes:
            plan += [f"size {size}", "wait 2500", "layout", f"shot {size}-{state}"]
    plan += ["mode DIGU", "wait 1500"]
    if args.panels:
        steps = [l.strip() for l in Path(args.panels).read_text().splitlines()]
        steps = [l for l in steps if l and not l.startswith("#")]
        if steps and steps[0].startswith("wait "):
            steps = steps[1:]
        for size in [s for s in args.panel_sizes.split(",") if s]:
            plan += [f"size {size}", "wait 3000"]
            plan += [f"shot {size}-{l.split(None, 1)[1]}" if l.startswith("shot ") else l for l in steps]
    plan_file = out / "sweep.plan"
    plan_file.write_text("\n".join(plan) + "\n")

    log_file = out / "sweep.log"
    timeout = 120 + 12 * len(sizes) + (300 * len(args.panel_sizes.split(",")) if args.panels else 0)
    with log_file.open("w") as log:
        rc = subprocess.run([str(app), "--font-px", args.font,
                             "--size", sizes[0], "--connect", args.radio, "--shot-plan", str(plan_file),
                             "--shot-dir", str(out)],
                            cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=timeout,
                            env={**__import__("os").environ, "QT_QPA_PLATFORM": "offscreen"}).returncode
    text = log_file.read_text(errors="replace")
    per_size = {}
    for line in text.splitlines():
        m = LAYOUT.search(line)
        if m:
            per_size.setdefault(m.group(1), []).append(line)
    for size, lines in per_size.items():
        (out / f"{size}.log").write_text("\n".join(lines) + "\n")
    full = text.count("maximum number of")
    qml = len(re.findall(r"\.qml:\d+", text))
    shots = len(list(out.glob("*.png")))
    print(f"exit {rc}, {len(per_size)} sizes laid out, {shots} pictures, qml warnings {qml}, radio full {full}")
    return 0 if rc == 0 and not full and not qml else 1


if __name__ == "__main__":
    sys.exit(main())
