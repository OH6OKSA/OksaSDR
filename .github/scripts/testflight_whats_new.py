#!/usr/bin/env python3
"""Set TestFlight "What to Test" for one build and hand it to the testers.

Env: API_KEY_ID, API_ISSUER_ID, API_KEY_P8 (key contents), BUNDLE_ID,
BUILD_NUMBER, NOTES_FILE, and optionally DESCRIPTION_FILE and
TESTFLIGHT_GROUPS. Sets the app's beta description (TestFlight's "App
Description") from DESCRIPTION_FILE, then waits for the uploaded build to
appear and creates or updates its beta build localization in the app's
primary locale. Then it waits for Apple to finish processing the build and
adds it to the beta groups, the ones named in TESTFLIGHT_GROUPS (comma
separated) or else every group that does not get each build by itself
(external groups, and internal ones without access to all builds), and
submits it for beta review when an external group got it.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import jwt  # PyJWT

API = "https://api.appstoreconnect.apple.com/v1"
WAIT_SECONDS = 45 * 60
POLL_SECONDS = 30
MAX_CHARS = 4000


def token() -> str:
    now = int(time.time())
    return jwt.encode(
        {"iss": os.environ["API_ISSUER_ID"], "iat": now, "exp": now + 15 * 60,
         "aud": "appstoreconnect-v1"},
        os.environ["API_KEY_P8"],
        algorithm="ES256",
        headers={"kid": os.environ["API_KEY_ID"], "typ": "JWT"},
    )


def call(method: str, path: str, body=None, query=None):
    url = API + path
    if query:
        url += "?" + urllib.parse.urlencode(query)
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": "Bearer " + token(),
        "Content-Type": "application/json",
    })
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as err:
        detail = err.read().decode(errors="replace")[:500]
        raise RuntimeError(f"{method} {path}: HTTP {err.code}: {detail}") from None


def main() -> int:
    notes = open(os.environ["NOTES_FILE"], encoding="utf-8").read().strip()
    if len(notes) > MAX_CHARS:
        print(f"notes are {len(notes)} characters; TestFlight allows {MAX_CHARS}", file=sys.stderr)
        return 1
    build_number = os.environ.get("BUILD_NUMBER", "").strip()

    apps = call("GET", "/apps", query={"filter[bundleId]": os.environ["BUNDLE_ID"]})["data"]
    if not apps:
        print("app not found for the bundle id", file=sys.stderr)
        return 1
    app = apps[0]
    locale = app["attributes"].get("primaryLocale") or "en-US"

    # The app's TestFlight description, kept in the repository so a rename
    # or new address reaches testers (it once still read "AetherSDR Mobile").
    description_file = os.environ.get("DESCRIPTION_FILE")
    if description_file and os.path.exists(description_file):
        description = open(description_file, encoding="utf-8").read().strip()
        if len(description) > MAX_CHARS:
            print(f"description is {len(description)} characters; TestFlight allows {MAX_CHARS}", file=sys.stderr)
            return 1
        localizations = call("GET", f"/apps/{app['id']}/betaAppLocalizations")["data"]
        current = next((l for l in localizations if l["attributes"].get("locale") == locale), None)
        if current and current["attributes"].get("description") == description:
            print(f"App Description already up to date ({locale})")
        elif current:
            call("PATCH", f"/betaAppLocalizations/{current['id']}", {"data": {
                "type": "betaAppLocalizations", "id": current["id"],
                "attributes": {"description": description}}})
            print(f"updated App Description ({locale})")
        else:
            call("POST", "/betaAppLocalizations", {"data": {
                "type": "betaAppLocalizations",
                "attributes": {"locale": locale, "description": description},
                "relationships": {"app": {"data": {"type": "apps", "id": app["id"]}}}}})
            print(f"set App Description ({locale})")

    if not build_number:
        print("no build number: App Description only")
        return 0

    deadline = time.time() + WAIT_SECONDS
    build = None
    while time.time() < deadline:
        found = call("GET", "/builds", query={
            "filter[app]": app["id"], "filter[version]": build_number, "limit": "1",
        })["data"]
        if found:
            build = found[0]
            break
        print(f"build {build_number} not in App Store Connect yet; waiting")
        time.sleep(POLL_SECONDS)
    if build is None:
        print(f"build {build_number} did not appear within {WAIT_SECONDS // 60} min", file=sys.stderr)
        return 1

    existing = call("GET", f"/builds/{build['id']}/betaBuildLocalizations")["data"]
    match = next((l for l in existing if l["attributes"].get("locale") == locale), None)
    if match:
        call("PATCH", f"/betaBuildLocalizations/{match['id']}", {"data": {
            "type": "betaBuildLocalizations", "id": match["id"],
            "attributes": {"whatsNew": notes}}})
        print(f"updated What to Test for build {build_number} ({locale})")
    else:
        call("POST", "/betaBuildLocalizations", {"data": {
            "type": "betaBuildLocalizations",
            "attributes": {"locale": locale, "whatsNew": notes},
            "relationships": {"build": {"data": {"type": "builds", "id": build["id"]}}}}})
        print(f"set What to Test for build {build_number} ({locale})")
    return 0 if distribute(app, build, build_number, deadline) else 1


def distribute(app, build, build_number: str, deadline: float) -> bool:
    """Adds the build to the beta groups once processed; False on a failure."""
    while True:
        state = call("GET", f"/builds/{build['id']}")["data"]["attributes"].get("processingState")
        if state == "VALID":
            break
        if state in ("FAILED", "INVALID"):
            print(f"build {build_number} is {state}: not handed to testers", file=sys.stderr)
            return False
        if time.time() > deadline:
            print(f"build {build_number} still {state} after waiting: add it to the group by hand",
                  file=sys.stderr)
            return False
        print(f"build {build_number} is {state}; waiting to hand it to the testers")
        time.sleep(POLL_SECONDS)

    groups = call("GET", f"/apps/{app['id']}/betaGroups", query={"limit": "50"})["data"]
    wanted = [n.strip() for n in os.environ.get("TESTFLIGHT_GROUPS", "").split(",") if n.strip()]
    if wanted:
        chosen = [g for g in groups if g["attributes"].get("name") in wanted]
        missing = set(wanted) - {g["attributes"].get("name") for g in chosen}
        if missing:
            print(f"no TestFlight group named {', '.join(sorted(missing))}", file=sys.stderr)
    else:
        chosen = [g for g in groups
                  if not g["attributes"].get("isInternalGroup")
                  or not g["attributes"].get("hasAccessToAllBuilds")]
    if not chosen:
        print("no TestFlight group to add the build to (internal groups get every build by themselves)")
        return not wanted

    ok = True
    external = False
    for group in chosen:
        name = group["attributes"].get("name")
        try:
            call("POST", f"/betaGroups/{group['id']}/relationships/builds",
                 {"data": [{"type": "builds", "id": build["id"]}]})
            print(f"build {build_number} added to TestFlight group '{name}'")
        except RuntimeError as err:
            print(f"could not add build {build_number} to '{name}': {err}", file=sys.stderr)
            ok = False
            continue
        if not group["attributes"].get("isInternalGroup"):
            external = True

    if external:
        # External testers get a build after beta review; after the first
        # build of a version Apple usually approves the next ones at once.
        try:
            call("POST", "/betaAppReviewSubmissions", {"data": {
                "type": "betaAppReviewSubmissions",
                "relationships": {"build": {"data": {"type": "builds", "id": build["id"]}}}}})
            print(f"build {build_number} submitted for beta review (external testers)")
        except RuntimeError as err:
            if "HTTP 409" in str(err):
                print(f"beta review: already submitted or not needed ({err})")
            else:
                print(f"could not submit build {build_number} for beta review: {err}", file=sys.stderr)
                ok = False
    return ok


if __name__ == "__main__":
    sys.exit(main())
