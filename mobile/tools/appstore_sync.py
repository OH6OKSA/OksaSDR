#!/usr/bin/env python3
"""Puts the App Store listing texts of mobile/appstore/ into App Store Connect.

Run on this machine (the API key is in ~/.config/oksasdr/asc.env, as for
testflight_testers.py). It edits only the App Store version that is still
editable (Prepare for Submission, or rejected); nothing is submitted.

    mobile/tools/appstore_sync.py              # show what would change
    mobile/tools/appstore_sync.py --apply      # write the texts
    mobile/tools/appstore_sync.py --apply --attach ~/Videos/oksasdr-tour-v5-720p.mp4
    mobile/tools/appstore_sync.py --apply --name   # also the store name (name.txt)

Written: the version's description, keywords, promotional text, support and
marketing URLs; the app's subtitle and privacy policy URL (the name only with
--name); App Review's notes (demo account not required). App Review's contact
(name, phone, e-mail), the age rating, App Privacy, the price and the DSA
status stay the user's, in App Store Connect. --attach uploads a file (the
review video) as an App Review attachment, once per file name.
"""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testflight_testers as tt  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEXTS = os.path.join(ROOT, "mobile", "appstore", "en-US")
EDITABLE = {"PREPARE_FOR_SUBMISSION", "DEVELOPER_REJECTED", "REJECTED", "METADATA_REJECTED", "INVALID_BINARY"}


def text(name: str) -> str:
    with open(os.path.join(TEXTS, name + ".txt"), encoding="utf-8") as f:
        return f.read().strip()


class Api:
    def __init__(self):
        self.asc = tt.Asc(tt.load_env())

    def call(self, method: str, path: str, body=None, query=None):
        """A request, tried again on a server error or a refused token (App
        Store Connect answered both now and then on 2026-11-17)."""
        url = path if path.startswith("http") else tt.API + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        data = json.dumps(body).encode() if body is not None else None
        for attempt in range(6):
            req = urllib.request.Request(url, data=data, method=method, headers={
                "Authorization": "Bearer " + self.asc.token(), "Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=60) as resp:
                    raw = resp.read()
                    return json.loads(raw) if raw else {}
            except urllib.error.HTTPError as err:
                detail = err.read().decode(errors="replace")[:400]
                if (err.code >= 500 or err.code == 401) and attempt < 5:
                    time.sleep(5 + 5 * attempt)
                    continue
                raise RuntimeError(f"{method} {path}: HTTP {err.code}: {detail}") from None


def changes(current: dict, wanted: dict) -> dict:
    return {k: v for k, v in wanted.items() if (current.get(k) or "") != v}


def show(what: str, diff: dict):
    for key, value in diff.items():
        shown = value if len(value) < 70 else value[:67] + "..."
        print(f"  {what}.{key} -> {shown!r}")


def upload(api: Api, detail_id: str, path: str, apply: bool):
    name = os.path.basename(path)
    existing = api.call("GET", f"/appStoreReviewDetails/{detail_id}/appStoreReviewAttachments")["data"]
    if any(a["attributes"].get("fileName") == name for a in existing):
        print(f"  attachment {name}: already there")
        return
    size = os.path.getsize(path)
    print(f"  attachment {name} ({size // 1_000_000} MB)" + ("" if apply else ": would upload"))
    if not apply:
        return
    made = api.call("POST", "/appStoreReviewAttachments", {"data": {
        "type": "appStoreReviewAttachments",
        "attributes": {"fileName": name, "fileSize": size},
        "relationships": {"appStoreReviewDetail": {"data": {"type": "appStoreReviewDetails", "id": detail_id}}}}})["data"]
    md5 = hashlib.md5()
    with open(path, "rb") as f:
        for op in made["attributes"]["uploadOperations"]:
            f.seek(op["offset"])
            chunk = f.read(op["length"])
            md5.update(chunk)
            headers = {h["name"]: h["value"] for h in op.get("requestHeaders") or []}
            req = urllib.request.Request(op["url"], data=chunk, method=op["method"], headers=headers)
            with urllib.request.urlopen(req, timeout=300):
                pass
    api.call("PATCH", f"/appStoreReviewAttachments/{made['id']}", {"data": {
        "type": "appStoreReviewAttachments", "id": made["id"],
        "attributes": {"uploaded": True, "sourceFileChecksum": md5.hexdigest()}}})
    print(f"  attachment {name}: uploaded")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--apply", action="store_true", help="write (without it: only show the changes)")
    parser.add_argument("--name", action="store_true", help="also set the store name from name.txt")
    parser.add_argument("--attach", metavar="FILE", help="upload FILE as an App Review attachment")
    args = parser.parse_args()

    api = Api()
    bundle = api.asc.env.get("BUNDLE_ID", "fi.oksa.aethersdr")
    app = api.call("GET", "/apps", query={"filter[bundleId]": bundle})["data"][0]
    versions = api.call("GET", f"/apps/{app['id']}/appStoreVersions", query={"limit": "10"})["data"]
    version = next((v for v in versions if v["attributes"].get("platform") == "IOS"
                    and v["attributes"].get("appStoreState") in EDITABLE), None)
    if not version:
        print("no editable iOS App Store version (all submitted or live)", file=sys.stderr)
        return 1
    print(f"App Store version {version['attributes']['versionString']} ({version['attributes']['appStoreState']})"
          + ("" if args.apply else "; showing, nothing written"))

    # The version's texts.
    loc = next(l for l in api.call("GET", f"/appStoreVersions/{version['id']}/appStoreVersionLocalizations")["data"]
               if l["attributes"]["locale"] == "en-US")
    diff = changes(loc["attributes"], {
        "description": text("description"), "keywords": text("keywords"),
        "promotionalText": text("promotional_text"), "supportUrl": text("support_url"),
        "marketingUrl": text("marketing_url")})
    show("version", diff)
    if diff and args.apply:
        api.call("PATCH", f"/appStoreVersionLocalizations/{loc['id']}", {"data": {
            "type": "appStoreVersionLocalizations", "id": loc["id"], "attributes": diff}})

    # The app's name, subtitle and privacy policy.
    info = next(i for i in api.call("GET", f"/apps/{app['id']}/appInfos")["data"]
                if i["attributes"].get("appStoreState") in EDITABLE)
    iloc = next(l for l in api.call("GET", f"/appInfos/{info['id']}/appInfoLocalizations")["data"]
                if l["attributes"]["locale"] == "en-US")
    wanted = {"subtitle": text("subtitle"), "privacyPolicyUrl": text("privacy_url")}
    if args.name:
        wanted["name"] = text("name")
    elif iloc["attributes"].get("name") != text("name"):
        print(f"  (the store name is {iloc['attributes'].get('name')!r}; name.txt says {text('name')!r}: --name sets it)")
    diff = changes(iloc["attributes"], wanted)
    show("app", diff)
    if diff and args.apply:
        api.call("PATCH", f"/appInfoLocalizations/{iloc['id']}", {"data": {
            "type": "appInfoLocalizations", "id": iloc["id"], "attributes": diff}})

    # App Review's notes (the contact stays the user's).
    detail = api.call("GET", f"/appStoreVersions/{version['id']}/appStoreReviewDetail").get("data")
    notes = text("review_notes")
    if not detail:
        print("  review.notes -> (new)")
        if args.apply:
            detail = api.call("POST", "/appStoreReviewDetails", {"data": {
                "type": "appStoreReviewDetails",
                "attributes": {"notes": notes, "demoAccountRequired": False},
                "relationships": {"appStoreVersion": {"data": {"type": "appStoreVersions", "id": version["id"]}}}}})["data"]
    else:
        diff = changes(detail["attributes"], {"notes": notes})
        show("review", diff)
        if diff and args.apply:
            api.call("PATCH", f"/appStoreReviewDetails/{detail['id']}", {"data": {
                "type": "appStoreReviewDetails", "id": detail["id"],
                "attributes": dict(diff, demoAccountRequired=False)}})
        contact = [k for k in ("contactFirstName", "contactLastName", "contactPhone", "contactEmail")
                   if not detail["attributes"].get(k)]
        if contact:
            print(f"  (App Review's contact is empty: {', '.join(contact)}; the user fills it in App Store Connect)")

    if args.attach:
        if not detail:
            print("  attachment: App Review's details are made by --apply first", file=sys.stderr)
            return 1
        upload(api, detail["id"], os.path.expanduser(args.attach), args.apply)
    return 0


if __name__ == "__main__":
    sys.exit(main())
