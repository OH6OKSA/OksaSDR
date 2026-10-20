#!/usr/bin/env python3
"""TestFlight testers: who tests, how much, and on which devices.

Run on this machine, not in CI: the repository is public, and a workflow's
log would publish the testers' names and devices.

    mobile/tools/testflight_testers.py            # every tester
    mobile/tools/testflight_testers.py michel     # names containing "michel"
    mobile/tools/testflight_testers.py --days 90  # usage over 90 days

The App Store Connect API key comes from ~/.config/oksasdr/asc.env (or the
file in ASC_ENV), with lines
    API_KEY_ID=...
    API_ISSUER_ID=...
    API_KEY_P8_FILE=/path/to/AuthKey_XXXX.p8
and optionally BUNDLE_ID (default fi.oksa.aethersdr, the OksaSDR app). Needs PyJWT with
crypto (pip install 'pyjwt[crypto]').

Each tester's devices (model, OS and the installed build) come from the
tester's appDevices, as on App Store Connect's tester page, and their groups
from betaGroups; sessions, crashes and feedback counts from the beta usage
metrics, and the devices of feedback and crash reports from those reports.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

try:
    import jwt  # PyJWT
except ImportError:
    sys.exit("PyJWT is missing: pip install 'pyjwt[crypto]'")

API = "https://api.appstoreconnect.apple.com/v1"


def load_env() -> dict:
    path = os.environ.get("ASC_ENV", os.path.expanduser("~/.config/oksasdr/asc.env"))
    if not os.path.exists(path):
        sys.exit(f"{path} not found: see the top of this script for its lines")
    env = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            env[key.strip()] = value.strip().strip('"').strip("'")
    for key in ("API_KEY_ID", "API_ISSUER_ID", "API_KEY_P8_FILE"):
        if not env.get(key):
            sys.exit(f"{path}: {key} is missing")
    env["API_KEY_P8"] = open(os.path.expanduser(env["API_KEY_P8_FILE"]), encoding="utf-8").read()
    return env


class Asc:
    def __init__(self, env: dict):
        self.env = env

    def token(self) -> str:
        now = int(time.time())
        return jwt.encode(
            {"iss": self.env["API_ISSUER_ID"], "iat": now, "exp": now + 15 * 60, "aud": "appstoreconnect-v1"},
            self.env["API_KEY_P8"],
            algorithm="ES256",
            headers={"kid": self.env["API_KEY_ID"], "typ": "JWT"},
        )

    def get(self, path: str, query=None) -> dict:
        url = path if path.startswith("http") else API + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        req = urllib.request.Request(url, headers={"Authorization": "Bearer " + self.token()})
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                return json.loads(resp.read() or b"{}")
        except urllib.error.HTTPError as err:
            detail = err.read().decode(errors="replace")[:300]
            raise RuntimeError(f"GET {path}: HTTP {err.code}: {detail}") from None

    def all(self, path: str, query=None):
        """Every page of a list; yields (resource, included-by-id)."""
        page = self.get(path, query)
        while True:
            included = {(i["type"], i["id"]): i for i in page.get("included", [])}
            for item in page.get("data", []):
                yield item, included
            nxt = page.get("links", {}).get("next")
            if not nxt:
                return
            page = self.get(nxt)


# Apple's model identifiers (underscore form, as the API gives them).
MODEL_NAMES = {
    "iPhone14_4": "iPhone 13 mini", "iPhone14_5": "iPhone 13", "iPhone14_2": "iPhone 13 Pro",
    "iPhone14_3": "iPhone 13 Pro Max", "iPhone14_6": "iPhone SE (3rd gen)", "iPhone14_7": "iPhone 14",
    "iPhone14_8": "iPhone 14 Plus", "iPhone15_2": "iPhone 14 Pro", "iPhone15_3": "iPhone 14 Pro Max",
    "iPhone15_4": "iPhone 15", "iPhone15_5": "iPhone 15 Plus", "iPhone16_1": "iPhone 15 Pro",
    "iPhone16_2": "iPhone 15 Pro Max", "iPhone17_1": "iPhone 16 Pro", "iPhone17_2": "iPhone 16 Pro Max",
    "iPhone17_3": "iPhone 16", "iPhone17_4": "iPhone 16 Plus", "iPhone17_5": "iPhone 16e",
    "iPhone18_1": "iPhone 17 Pro", "iPhone18_2": "iPhone 17 Pro Max", "iPhone18_3": "iPhone 17",
    "iPhone18_4": "iPhone Air",
    "iPad14_1": "iPad mini 6", "iPad14_2": "iPad mini 6", "iPad16_1": "iPad mini (A17 Pro)",
    "iPad16_2": "iPad mini (A17 Pro)", "iPad13_18": "iPad (10th gen)", "iPad13_19": "iPad (10th gen)",
    "iPad15_7": "iPad (A16)", "iPad15_8": "iPad (A16)", "iPad13_1": "iPad Air 4", "iPad13_2": "iPad Air 4",
    "iPad13_16": "iPad Air 5", "iPad13_17": "iPad Air 5", "iPad14_8": "iPad Air 11 (M2)",
    "iPad14_9": "iPad Air 11 (M2)", "iPad14_10": "iPad Air 13 (M2)", "iPad14_11": "iPad Air 13 (M2)",
    "iPad15_3": "iPad Air 11 (M3)", "iPad15_4": "iPad Air 11 (M3)", "iPad15_5": "iPad Air 13 (M3)",
    "iPad15_6": "iPad Air 13 (M3)", "iPad13_4": "iPad Pro 11 (3rd gen)", "iPad13_5": "iPad Pro 11 (3rd gen)",
    "iPad13_6": "iPad Pro 11 (3rd gen)", "iPad13_7": "iPad Pro 11 (3rd gen)",
    "iPad13_8": "iPad Pro 12.9 (5th gen)", "iPad13_9": "iPad Pro 12.9 (5th gen)",
    "iPad13_10": "iPad Pro 12.9 (5th gen)", "iPad13_11": "iPad Pro 12.9 (5th gen)",
    "iPad14_3": "iPad Pro 11 (4th gen)", "iPad14_4": "iPad Pro 11 (4th gen)",
    "iPad14_5": "iPad Pro 12.9 (6th gen)", "iPad14_6": "iPad Pro 12.9 (6th gen)",
    "iPad16_3": "iPad Pro 11 (M4)", "iPad16_4": "iPad Pro 11 (M4)", "iPad16_5": "iPad Pro 13 (M4)",
    "iPad16_6": "iPad Pro 13 (M4)", "iPad8_11": "iPad Pro 12.9 (4th gen)", "iPad8_12": "iPad Pro 12.9 (4th gen)",
    "iPad11_3": "iPad Air 3", "iPad11_4": "iPad Air 3", "iPad12_1": "iPad (9th gen)", "iPad12_2": "iPad (9th gen)",
    "iPad17_1": "iPad Pro 11 (M5)", "iPad17_2": "iPad Pro 11 (M5)", "iPad17_3": "iPad Pro 13 (M5)",
    "iPad17_4": "iPad Pro 13 (M5)", "Mac14_2": "MacBook Air (M2)",
    "Mac17_4": "MacBook Air 15 (M5)",
}


def model_name(model: str) -> str:
    model = (model or "?").replace(",", "_")
    name = MODEL_NAMES.get(model)
    return f"{name} ({model})" if name else model


def tester_name(attrs: dict) -> str:
    name = " ".join(p for p in (attrs.get("firstName"), attrs.get("lastName")) if p)
    return name or "(no name)"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("name", nargs="?", default="", help="show testers whose name contains this")
    parser.add_argument("--days", type=int, default=30, choices=(7, 30, 90, 365),
                        help="usage period in days (default 30)")
    parser.add_argument("--emails", action="store_true", help="show the testers' e-mail addresses")
    args = parser.parse_args()

    asc = Asc(load_env())
    bundle = asc.env.get("BUNDLE_ID", "fi.oksa.aethersdr")
    apps = asc.get("/apps", {"filter[bundleId]": bundle}).get("data", [])
    if not apps:
        sys.exit(f"no app with the bundle id {bundle}")
    app_id = apps[0]["id"]

    testers = {}
    for item, included in asc.all("/betaTesters", {"filter[apps]": app_id, "limit": 50, "include": "betaGroups"}):
        a = item["attributes"]
        groups = [included.get(("betaGroups", g["id"]), {}).get("attributes", {}).get("name", "?")
                  for g in ((item.get("relationships") or {}).get("betaGroups") or {}).get("data") or []]
        installs = [f'{model_name(d.get("model"))}, {d.get("platform") or ""} {d.get("osVersion") or "?"}: '
                    f'{d.get("appBuildVersion") or "?"}' for d in a.get("appDevices") or []]
        testers[item["id"]] = {"name": tester_name(a), "email": a.get("email") or "",
                               "state": a.get("state") or "", "groups": groups, "installs": installs,
                               "devices": {}, "usage": None}

    # Devices, from feedback and crash reports (newest first).
    for kind in ("betaFeedbackScreenshotSubmissions", "betaFeedbackCrashSubmissions"):
        try:
            for item, included in asc.all(f"/apps/{app_id}/{kind}",
                                          {"limit": 50, "include": "tester,build"}):
                a = item["attributes"]
                rel = item.get("relationships", {})
                tester_id = ((rel.get("tester") or {}).get("data") or {}).get("id")
                build = included.get(("builds", ((rel.get("build") or {}).get("data") or {}).get("id")))
                build_no = build["attributes"].get("version") if build else "?"
                device = f'{model_name(a.get("deviceModel"))}, {a.get("devicePlatform") or ""} {a.get("osVersion") or "?"}'
                entry = testers.setdefault(tester_id, {"name": "(tester left)", "email": a.get("email") or "",
                                                       "state": "", "groups": [], "installs": [],
                                                       "devices": {}, "usage": None})
                seen = entry["devices"].setdefault(device.strip(), {"builds": set(), "last": ""})
                seen["builds"].add(build_no)
                seen["last"] = max(seen["last"], (a.get("createdDate") or "")[:10])
        except RuntimeError as err:
            print(f"({kind}: {err})", file=sys.stderr)

    # Sessions, crashes and feedback per tester.
    try:
        metrics = asc.get(f"/apps/{app_id}/metrics/betaTesterUsages",
                          {"groupBy": "betaTesters", "period": f"P{args.days}D", "limit": 200})
        for row in metrics.get("data", []):
            dim = ((row.get("dimensions") or {}).get("betaTesters") or {}).get("data") or {}
            total = {"sessionCount": 0, "crashCount": 0, "feedbackCount": 0}
            for point in row.get("dataPoints", []):
                for key in total:
                    total[key] += int((point.get("values") or {}).get(key) or 0)
            if dim.get("id") in testers:
                testers[dim["id"]]["usage"] = total
    except RuntimeError as err:
        print(f"(usage metrics: {err})", file=sys.stderr)

    wanted = args.name.lower()
    shown = [t for t in testers.values() if wanted in t["name"].lower() or (wanted and wanted in t["email"].lower())]
    shown.sort(key=lambda t: t["name"].lower())
    for t in shown:
        header = t["name"] + (f' <{t["email"]}>' if args.emails and t["email"] else "")
        if t["state"]:
            header += f'  [{t["state"].lower()}]'
        if t["groups"]:
            header += "  (" + ", ".join(sorted(t["groups"])) + ")"
        print(header)
        for install in t["installs"]:
            print(f"  installed: {install}")
        u = t["usage"]
        if u:
            print(f'  last {args.days} days: {u["sessionCount"]} sessions, {u["crashCount"]} crashes, '
                  f'{u["feedbackCount"]} feedback')
        for device, seen in t["devices"].items():
            builds = ", ".join(sorted(seen["builds"], key=lambda b: int(b) if b.isdigit() else 0))
            print(f'  reports from {device}  (builds {builds}; last {seen["last"]})')
    if not shown:
        print("no tester matches" if wanted else "no testers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
