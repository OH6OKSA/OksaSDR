#!/usr/bin/env python3
"""Compares the layout decisions of two render audits, size by size.

    mobile/tools/layout-diff.py OLD_DIR NEW_DIR

Each directory holds one log per window size (WxH.log, as a render run's
output), with the lines a shot plan's `layout` command writes
("shot-plan: layout ..."; ShotPlan.h). A plan can log several, one per state
(DIGU, then USB, ...); they are compared in order. For every size in both it
prints the page and its decisions (landscape, flowHeader, infoUnder,
infoScale) and the visible info panels' geometry, old against new, and marks:

  !! a decision changed (page type, a flag both runs have, infoScale by
     more than 0.03, the number of info panels), or an info panel is now
     narrower than it needs (cut off at the right) or cut by more than
     before: look at both pictures and explain it before calling it fine;
  ~  an info panel moved or changed width by more than 8 px.

A property only one run has (one added since) is shown, not compared. The
tablet page's probes (shown at opacity 0, never seen) are left out.

A picture can look fine on its own while a decision regressed (build 137's
first audit: the iPhone Duo's inner screen put the info panel under the
frequency and left the room beside it empty). Exit status 1 if anything
changed, so a script can stop on it.
"""
import re
import sys
from pathlib import Path

LINE = re.compile(r"shot-plan: layout (.*)")
PART = re.compile(r"(\w+)\((.*?)\)")


def read(log: Path):
    """Every layout line of a run, in order, each as (pages, panels)."""
    states = []
    for line in LINE.findall(log.read_text(errors="replace")):
        pages, panels = [], []
        for name, body in PART.findall(line):
            props = dict(kv.split("=", 1) for kv in body.split() if "=" in kv)
            if name == "InfoPanel":
                panels.append(props)
            elif name != "Probe":
                pages.append((name, props))
        states.append((pages, panels))
    return states


def is_cut(p) -> bool:
    return int(p["w"]) < int(p["needs"]) - 1


def describe(pages, panels):
    page = "; ".join(f"{n}({' '.join(f'{k}={v}' for k, v in p.items())})" for n, p in pages)
    boxes = " ".join(f"[x{p['x']} y{p['y']} w{p['w']} needs{p['needs']}]" for p in panels)
    return page, boxes


def report(label, old, new) -> bool:
    (op, opan), (np_, npan) = old, new
    marks = set()
    if [n for n, _ in op] != [n for n, _ in np_] or len(opan) != len(npan):
        marks.add("!!")
    else:
        for (_, a), (_, b) in zip(op, np_):
            if any(a[k] != b[k] for k in (set(a) & set(b)) - {"infoScale"}):
                marks.add("!!")
            if abs(float(a.get("infoScale", 1)) - float(b.get("infoScale", 1))) > 0.03:
                marks.add("!!")
        for a, b in zip(opan, npan):
            if any(abs(int(a[k]) - int(b[k])) > 8 for k in ("x", "y", "w")):
                marks.add("~")
            if is_cut(b) and (not is_cut(a) or int(b["needs"]) - int(b["w"]) > int(a["needs"]) - int(a["w"]) + 1):
                marks.add("!!")
    mark = "!!" if "!!" in marks else ("~ " if marks else "  ")
    o_page, o_boxes = describe(op, opan)
    n_page, n_boxes = describe(np_, npan)
    print(f"{mark} {label}")
    if o_page == n_page:
        print(f"     page  {n_page}")
    else:
        print(f"     old   {o_page}\n     new   {n_page}")
    if o_boxes == n_boxes:
        print(f"     info  {n_boxes}")
    else:
        print(f"     old   {o_boxes}\n     new   {n_boxes}")
    for i, b in enumerate(npan):
        if is_cut(b):
            before = i < len(opan) and is_cut(opan[i])
            print(f"     cut   the info panel at x{b['x']} y{b['y']} has {b['w']} of the {b['needs']} px it needs"
                  + (f" (before: {opan[i]['w']} of {opan[i]['needs']})" if before else ""))
    return bool(marks)


def size_key(s):
    return [int(n) for n in s.split("x")] if re.fullmatch(r"\d+x\d+", s) else [0]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    old_dir, new_dir = Path(sys.argv[1]), Path(sys.argv[2])
    changed = False
    sizes = sorted({p.stem for p in old_dir.glob("*.log")} & {p.stem for p in new_dir.glob("*.log")}, key=size_key)
    for size in sizes:
        old, new = read(old_dir / f"{size}.log"), read(new_dir / f"{size}.log")
        if not old or not new:
            print(f"!! {size}: no layout line in the {'old' if not old else 'new'} run")
            changed = True
            continue
        many = max(len(old), len(new)) > 1
        for n, (o, w) in enumerate(zip(old, new), 1):
            changed = report(f"{size} #{n}" if many else size, o, w) or changed
        if len(old) != len(new):
            print(f"!! {size}: {len(old)} layout lines in the old run, {len(new)} in the new")
            changed = True
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
