#!/usr/bin/env python3
"""A stand-in for the sign-in service's change-password call, for tests of "Forgot password?" (roadmap 157).

    auth0-mock.py PORT [MODE]

Listens on 127.0.0.1:PORT for POST /dbconnections/change_password and answers by MODE:
ok (200, the service's plain text), notfound (404), badrequest (400 with a JSON error),
ratelimit (429), slow (no answer for 30 s). Each request is printed with its JSON keys,
the e-mail masked; nothing leaves this machine. Run the app with
OKSASDR_TEST_AUTH0=http://127.0.0.1:PORT.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

mode = sys.argv[2] if len(sys.argv) > 2 else 'ok'


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get('Content-Length', '0') or 0))
        try:
            body = json.loads(raw or b'{}')
        except ValueError:
            body = {'unparsed': raw[:80].decode('latin1')}
        shown = {k: ('***' if k == 'email' else ('[client id]' if k == 'client_id' else v)) for k, v in body.items()}
        print('request: POST %s content-type=%s body=%s email_given=%s' % (
            self.path, self.headers.get('Content-Type'), json.dumps(shown, sort_keys=True), bool(body.get('email'))), flush=True)
        if self.path != '/dbconnections/change_password':
            self.send_response(404); self.end_headers(); return
        if mode == 'slow':
            time.sleep(30)
        code, text, ctype = {
            'ok': (200, "\"We've just sent you an email to reset your password.\"", 'text/html; charset=utf-8'),
            'notfound': (404, '{"error":"not_found","error_description":"user not found"}', 'application/json'),
            'badrequest': (400, '{"error":"invalid_request","error_description":"connection is required"}', 'application/json'),
            'ratelimit': (429, '{"error":"too_many_requests","error_description":"Global limit has been reached"}', 'application/json'),
        }.get(mode, (200, 'ok', 'text/plain'))
        data = text.encode()
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


HTTPServer(('127.0.0.1', int(sys.argv[1])), Handler).serve_forever()
