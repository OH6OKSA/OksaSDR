#!/usr/bin/env python3
"""TestFlight feedback: the testers' comments, screenshots and crash reports.

Run on this machine, not in CI: the repository is public, and a workflow's
log would publish the testers' names and words.

    mobile/tools/testflight_feedback.py           # the newest 20, unseen marked NEW
    mobile/tools/testflight_feedback.py --new     # only what came since the last run
    mobile/tools/testflight_feedback.py --limit 50

The API key comes from the same file as testflight_testers.py's. Screenshots
and crash logs are saved under ~/.cache/oksasdr/feedback (or --out); the
newest report's time is kept in ~/.config/oksasdr/feedback-seen, so the next
run marks what is new (--peek leaves it as it was).
"""
import argparse
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testflight_testers import Asc, load_env, model_name, tester_name  # noqa: E402

SEEN = os.path.expanduser("~/.config/oksasdr/feedback-seen")
KINDS = (("betaFeedbackScreenshotSubmissions", "FEEDBACK"), ("betaFeedbackCrashSubmissions", "CRASH"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--limit", type=int, default=20, help="reports of each kind (default 20)")
    parser.add_argument("--new", action="store_true", help="only reports since the last run")
    parser.add_argument("--peek", action="store_true", help="do not mark these reports seen")
    parser.add_argument("--out", default=os.path.expanduser("~/.cache/oksasdr/feedback"),
                        help="where screenshots and crash logs go")
    args = parser.parse_args()

    seen = open(SEEN, encoding="utf-8").read().strip() if os.path.exists(SEEN) else ""
    asc = Asc(load_env())
    bundle = asc.env.get("BUNDLE_ID", "fi.oksa.aethersdr")
    apps = asc.get("/apps", {"filter[bundleId]": bundle}).get("data", [])
    if not apps:
        sys.exit(f"no app with the bundle id {bundle}")
    app_id = apps[0]["id"]

    rows = []
    for kind, label in KINDS:
        count = 0
        for item, included in asc.all(f"/apps/{app_id}/{kind}",
                                      {"limit": 50, "include": "tester,build", "sort": "-createdDate"}):
            a = item["attributes"]
            rel = item.get("relationships", {})
            tester = included.get(("betaTesters", ((rel.get("tester") or {}).get("data") or {}).get("id")))
            build = included.get(("builds", ((rel.get("build") or {}).get("data") or {}).get("id")))
            rows.append({"date": a.get("createdDate") or "", "kind": label, "id": item["id"], "a": a,
                         "who": tester_name(tester["attributes"]) if tester else "(tester left)",
                         "build": build["attributes"].get("version") if build else "?"})
            count += 1
            if count >= args.limit:
                break
    rows.sort(key=lambda r: r["date"], reverse=True)
    if args.new:
        rows = [r for r in rows if r["date"] > seen]
        if not rows:
            print(f"nothing new since {seen[:16] or 'the start'}")

    os.makedirs(args.out, exist_ok=True)
    for r in rows:
        a = r["a"]
        new = "NEW " if r["date"] > seen else ""
        print(f'=== {new}{r["date"][:16]} {r["kind"]} build {r["build"]}, {r["who"]}')
        print(f'    {model_name(a.get("deviceModel"))}, {a.get("devicePlatform") or ""} {a.get("osVersion") or "?"}, '
              f'{a.get("screenWidthInPoints")}x{a.get("screenHeightInPoints")} pt, {a.get("connectionType")}, '
              f'uptime {int((a.get("appUptimeInMilliseconds") or 0) / 1000)} s, {a.get("locale")} {a.get("timeZone")}')
        if a.get("comment"):
            print("    comment: " + a["comment"].strip().replace("\n", "\n             "))
        stem = os.path.join(args.out, f'{r["date"][:10]}_{r["id"][:8]}')
        for i, shot in enumerate(a.get("screenshots") or []):
            path = f"{stem}_{i}.png"
            if not os.path.exists(path):
                try:
                    urllib.request.urlretrieve(shot["url"], path)
                except OSError as err:
                    path = f"(not saved: {err})"
            print(f"    screenshot: {path}")
        if r["kind"] == "CRASH":
            path = f"{stem}_crash.txt"
            if not os.path.exists(path):
                try:
                    log = asc.get(f'/betaFeedbackCrashSubmissions/{r["id"]}/crashLog')["data"]["attributes"]
                    open(path, "w", encoding="utf-8").write(log.get("logText") or "")
                except RuntimeError:
                    path = "(the API gives no crash log for this report)"
            print(f"    crash log: {path}")

    newest = max([r["date"] for r in rows] + [seen])
    if not args.peek and newest > seen:
        os.makedirs(os.path.dirname(SEEN), exist_ok=True)
        open(SEEN, "w", encoding="utf-8").write(newest + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
