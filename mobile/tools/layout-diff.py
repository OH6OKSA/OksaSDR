#!/usr/bin/env python3
"""Compares the layout decisions of two render audits, size by size.

    mobile/tools/layout-diff.py OLD_DIR NEW_DIR

Each directory holds one log per window size (WxH.log, as a render run's
output), with the line a shot plan's `layout` command writes
("shot-plan: layout ..."; ShotPlan.h). For every size in both it prints the
page and its decisions (landscape, flowHeader, infoUnder, infoScale) and the
visible info panels' geometry, old against new, and marks what changed:

  !! a decision changed (page type, a flag, the number of info panels):
     look at both pictures and explain it before calling the change fine;
  ~  an info panel moved or changed width by more than 8 px.

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
    text = log.read_text(errors="replace")
    found = LINE.findall(text)
    if not found:
        return None
    line = found[0]
    pages, panels = [], []
    for name, body in PART.findall(line):
        props = dict(kv.split("=", 1) for kv in body.split() if "=" in kv)
        (panels if name == "InfoPanel" else pages).append((name, props))
    return pages, panels


def describe(pages, panels):
    page = "; ".join(f"{n}({' '.join(f'{k}={v}' for k, v in p.items())})" for n, p in pages)
    boxes = " ".join(f"[x{p['x']} y{p['y']} w{p['w']} needs{p['needs']}]" for _, p in panels)
    return page, boxes


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    old_dir, new_dir = Path(sys.argv[1]), Path(sys.argv[2])
    changed = False
    sizes = sorted({p.stem for p in old_dir.glob("*.log")} & {p.stem for p in new_dir.glob("*.log")},
                   key=lambda s: [int(n) for n in s.split("x")] if re.fullmatch(r"\d+x\d+", s) else [0])
    for size in sizes:
        old, new = read(old_dir / f"{size}.log"), read(new_dir / f"{size}.log")
        if old is None or new is None:
            print(f"{size}: no layout line in {'old' if old is None else 'new'} run")
            continue
        (op, opan), (np_, npan) = old, new
        marks = []
        if [(n, {k: v for k, v in p.items() if k != "infoScale"}) for n, p in op] != \
           [(n, {k: v for k, v in p.items() if k != "infoScale"}) for n, p in np_] or len(opan) != len(npan):
            marks.append("!!")
        else:
            for (_, a), (_, b) in zip(op, np_):
                if abs(float(a.get("infoScale", 1)) - float(b.get("infoScale", 1))) > 0.03:
                    marks.append("!!")
            for (_, a), (_, b) in zip(opan, npan):
                if any(abs(int(a[k]) - int(b[k])) > 8 for k in ("x", "y", "w")):
                    marks.append("~")
        mark = "!!" if "!!" in marks else ("~ " if marks else "  ")
        changed = changed or bool(marks)
        o_page, o_boxes = describe(op, opan)
        n_page, n_boxes = describe(np_, npan)
        print(f"{mark} {size}")
        if o_page == n_page:
            print(f"     page  {n_page}")
        else:
            print(f"     old   {o_page}\n     new   {n_page}")
        if o_boxes == n_boxes:
            print(f"     info  {n_boxes}")
        else:
            print(f"     old   {o_boxes}\n     new   {n_boxes}")
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
