#!/usr/bin/env python3
"""Add one TestFlight tester to the external group TestGroup (App Store
Connect API, the credentials testflight_testers.py reads at run time).

    python3 mobile/tools/add_one_tester.py FIRST LAST EMAIL

Only on the user's word for that person. Then check that
testflight_testers.py lists them as [invited] and send the welcome reply.
"""
import json, sys, urllib.request, urllib.error
sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import testflight_testers as tt
asc = tt.Asc(tt.load_env())
app_id = asc.get("/apps", {"filter[bundleId]": asc.env.get("BUNDLE_ID", "fi.oksa.aethersdr")})["data"][0]["id"]
groups = asc.get("/betaGroups", {"filter[app]": app_id, "filter[name]": "TestGroup"})["data"]
gid = groups[0]["id"]
for first, last, email in [(sys.argv[1], sys.argv[2], sys.argv[3])]:
    body = {"data": {"type": "betaTesters",
                     "attributes": {"firstName": first, "lastName": last, "email": email},
                     "relationships": {"betaGroups": {"data": [{"type": "betaGroups", "id": gid}]}}}}
    req = urllib.request.Request(tt.API + "/betaTesters", data=json.dumps(body).encode(), method="POST",
                                 headers={"Authorization": "Bearer " + asc.token(), "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            print(first, last, "HTTP", r.status)
    except urllib.error.HTTPError as e:
        print(first, last, "HTTP", e.code, e.read().decode(errors="replace")[:200])
