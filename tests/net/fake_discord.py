#!/usr/bin/env python3
"""A stand-in for Discord's gateway and API, and the tests of how dm-cli
treats them: what Discord's documentation asks of a client (resuming,
heartbeats, close codes, rate limits) and what keeps it from hammering
Discord whatever Discord does (backoff, the cap on fresh logins, no
repeated requests).  run.sh builds dm-cli and calls this in a container.

  fake_discord.py --client DM_CLI --ca CA --cert CERT --key KEY [TEST...]
  fake_discord.py --list
"""

import argparse
import asyncio
import http.server
import json
import os
import ssl
import subprocess
import sys
import tempfile
import threading
import time

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

GW_PORT, API_PORT = 8443, 8444
T0 = time.monotonic()


def now():
    return time.monotonic()


class State:
    """What the stand-in does (the test sets it) and saw (the test reads it)."""

    def reset(self):
        self.events = []        # (time, kind, details)
        self.hb_interval = 3000
        self.ack = True
        self.on_ready = None    # async fn(ws): after READY or RESUMED
        self.on_identify = None # async fn(ws) -> True: answered it (no READY)
        self.seq = 0
        self.api = {}           # path -> list of (status, headers, body) answers, the last repeated
        self.lock = threading.Lock()

    def log(self, kind, **kw):
        with self.lock:
            self.events.append((now(), kind, kw))
        print("  %7.2f %-10s %s" % (now() - T0, kind, kw), flush=True)

    def of(self, kind):
        with self.lock:
            return [e for e in self.events if e[1] == kind]


S = State()
S.reset()


def ready_payload():
    S.seq += 1
    return {"op": 0, "t": "READY", "s": S.seq, "d": {
        "v": 9, "session_id": "sess1", "session_type": "normal",
        "resume_gateway_url": "wss://localhost:%d/resume" % GW_PORT,
        "user": {"id": "1000", "username": "tester", "global_name": "Tester", "avatar": None, "discriminator": "0"},
        "user_settings_proto": "", "guilds": [], "users": [], "private_channels": [], "relationships": [],
        "sessions": [{"session_id": "sess1", "status": "online", "activities": [], "client_info": {}}],
        "read_state": {"entries": [], "version": 1, "partial": False},
        "user_guild_settings": {"entries": [], "version": 1, "partial": False},
    }}


async def gateway(ws):
    path = ws.request.path
    S.log("connect", path=path)
    await ws.send(json.dumps({"op": 10, "d": {"heartbeat_interval": S.hb_interval}}))
    S.log("hello")
    try:
        async for raw in ws:
            m = json.loads(raw)
            op = m.get("op")
            if op == 1:
                S.log("heartbeat", seq=m.get("d"))
                if S.ack:
                    await ws.send(json.dumps({"op": 11}))
            elif op == 2:
                S.log("identify")
                if S.on_identify and await S.on_identify(ws):
                    continue
                await ws.send(json.dumps(ready_payload()))
                if S.on_ready:
                    asyncio.ensure_future(S.on_ready(ws))
            elif op == 6:
                d = m.get("d", {})
                S.log("resume", session=d.get("session_id"), seq=d.get("seq"))
                S.seq += 1
                await ws.send(json.dumps({"op": 0, "t": "RESUMED", "s": S.seq, "d": {}}))
            else:
                S.log("op%s" % op)
    except ConnectionClosed:
        pass
    S.log("closed", code=ws.close_code)


class Api(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def answer(self):
        path = self.path.split("?")[0]
        S.log("api", method=self.command, path=path, auth=bool(self.headers.get("Authorization")))
        with S.lock:
            queue = S.api.get(path)
            if queue:
                status, headers, body = queue[0] if len(queue) == 1 else queue.pop(0)
            elif path.endswith("/gateway"):
                status, headers, body = 200, {}, {"url": "wss://localhost:%d" % GW_PORT}
            else:
                status, headers, body = 200, {}, {}
        data = body.encode() if isinstance(body, str) else json.dumps(body).encode()
        self.send_response(status)
        if "Content-Type" not in headers:
            self.send_header("Content-Type", "application/json")
        for k, v in headers.items():
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = answer


# ---- the client ----------------------------------------------------------

class Client:
    def __init__(self, args, token=True):
        self.home = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.home, ".discordmessenger"))
        with open(os.path.join(self.home, ".discordmessenger", "settings.json"), "w") as f:
            json.dump({"Token": "test-token-0123456789", "DiscordAPI": "https://localhost:%d/api/v9/" % API_PORT,
                       "EnableTLSVerification": True}, f)
        env = dict(os.environ, HOME=self.home, DM_CA_FILE=OPTS.ca)
        if token:
            env["DM_TOKEN"] = "test-token-0123456789"
        self.log = open(os.path.join(self.home, "client.log"), "w")
        self.p = subprocess.Popen([OPTS.client] + list(args), env=env, stdout=self.log, stderr=subprocess.STDOUT)

    def stop(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                self.p.kill()

    def output(self):
        self.log.flush()
        return open(self.log.name).read()


async def wait_for(cond, timeout, what):
    end = now() + timeout
    while now() < end:
        if cond():
            return
        await asyncio.sleep(0.05)
    raise AssertionError("timed out waiting for " + what)


def check(ok, what):
    if not ok:
        raise AssertionError(what)
    print("  ok: " + what, flush=True)


# ---- the tests -------------------------------------------------------------

async def t_heartbeats():
    """first heartbeat at a random part of the interval, then every interval;
    one login, and no API request asked for twice"""
    S.hb_interval = 3000
    c = Client([])
    try:
        await wait_for(lambda: len(S.of("heartbeat")) >= 3, 15, "3 heartbeats")
        hello = S.of("hello")[0][0]
        hbs = [e[0] for e in S.of("heartbeat")]
        check(0 <= hbs[0] - hello < 3.0, "first heartbeat %.2f s after HELLO, within the interval" % (hbs[0] - hello))
        gaps = [b - a for a, b in zip(hbs, hbs[1:])]
        check(all(2.8 < g < 3.4 for g in gaps), "then every 3 s (%s)" % ", ".join("%.2f" % g for g in gaps))
        check(len(S.of("identify")) == 1 and len(S.of("connect")) == 1, "one connection, one login")
        paths = [e[2]["path"] for e in S.of("api")]
        dup = sorted(set(p for p in paths if paths.count(p) > 1))
        check(not dup, "no API request repeated (%d requests)" % len(paths))
    finally:
        c.stop()


async def drop_then_expect_resume(action, label):
    S.on_ready = action
    c = Client([])
    try:
        await wait_for(lambda: len(S.of("connect")) >= 2, 15, "a second connection")
        closed = S.of("closed")[0][0]
        second = S.of("connect")[1]
        check(second[2]["path"].startswith("/resume"), "%s: reconnected to the session's own address" % label)
        check(0.3 < second[0] - closed < 3.0, "%s: after %.2f s (backoff, not at once)" % (label, second[0] - closed))
        await wait_for(lambda: S.of("resume"), 5, "RESUME")
        r = S.of("resume")[0][2]
        check(r["session"] == "sess1" and r["seq"] == 1, "%s: RESUME with the session and the last sequence (%s)" % (label, r))
        check(len(S.of("identify")) == 1, "%s: no second login" % label)
    finally:
        c.stop()


async def t_resume_after_close():
    """Discord closes (1001): the client resumes, after a short wait"""
    async def act(ws):
        await asyncio.sleep(1)
        await ws.close(code=1001)
    await drop_then_expect_resume(act, "close 1001")


async def t_resume_after_reconnect_op():
    """op 7 RECONNECT: the client reconnects and resumes"""
    async def act(ws):
        await asyncio.sleep(1)
        await ws.send(json.dumps({"op": 7, "d": None}))
    await drop_then_expect_resume(act, "op 7")


async def t_invalid_session():
    """op 9 (not resumable): a fresh login, 1 to 5 s later"""
    async def act(ws):
        S.on_ready = None
        await asyncio.sleep(1)
        await ws.send(json.dumps({"op": 9, "d": False}))
    S.on_ready = act
    c = Client([])
    try:
        await wait_for(lambda: len(S.of("identify")) >= 2, 15, "a second login")
        sent = [e for e in S.events if e[1] == "hello"][0][0] + 1
        second = S.of("connect")[1]
        check(not second[2]["path"].startswith("/resume"), "connected to the gateway, not the old session")
        check(0.9 < second[0] - sent < 6.5, "after %.2f s (1 to 5 s, as Discord asks)" % (second[0] - sent))
        check(not S.of("resume"), "no RESUME of the invalid session")
    finally:
        c.stop()


async def t_dead_connection():
    """heartbeats not acknowledged: the client notices, reconnects, resumes"""
    S.hb_interval = 2000
    S.ack = False
    c = Client([])
    try:
        await wait_for(lambda: len(S.of("connect")) >= 2, 12, "a reconnection")
        check(len(S.of("heartbeat")) == 1, "one unanswered heartbeat, then no more on that connection")
        await wait_for(lambda: S.of("resume"), 5, "RESUME")
        check(True, "resumed on a new connection")
    finally:
        c.stop()


async def t_heartbeat_request():
    """op 1 from Discord: a heartbeat at once"""
    S.hb_interval = 30000
    async def act(ws):
        await asyncio.sleep(1)
        S.log("asked")
        await ws.send(json.dumps({"op": 1, "d": None}))
    S.on_ready = act
    c = Client([])
    try:
        await wait_for(lambda: S.of("heartbeat"), 5, "a heartbeat")
        dt = S.of("heartbeat")[0][0] - S.of("asked")[0][0]
        check(0 <= dt < 0.5, "heartbeat %.2f s after being asked" % dt)
    finally:
        c.stop()


async def t_auth_failed():
    """close 4004 (the token is not good): never reconnects"""
    async def act(ws):
        await asyncio.sleep(1)
        await ws.close(code=4004)
    S.on_ready = act
    c = Client([])
    try:
        await wait_for(lambda: S.of("closed"), 5, "the close")
        await asyncio.sleep(8)
        check(len(S.of("connect")) == 1, "no reconnection in 8 s")
    finally:
        c.stop()


async def t_rate_limited_close():
    """close 4008 (sent too fast): at least a minute before reconnecting"""
    async def act(ws):
        S.on_ready = None
        await asyncio.sleep(1)
        await ws.close(code=4008)
    S.on_ready = act
    c = Client([])
    try:
        await wait_for(lambda: S.of("closed"), 5, "the close")
        closed = S.of("closed")[0][0]
        await wait_for(lambda: len(S.of("connect")) >= 2, 75, "the reconnection")
        dt = S.of("connect")[1][0] - closed
        check(60 <= dt < 70, "reconnected %.1f s later" % dt)
    finally:
        c.stop()


async def t_login_cap():
    """every login refused (op 9): the waits grow, and after 10 logins in the
    hour the client stops"""
    async def refuse(ws):
        await ws.send(json.dumps({"op": 9, "d": False}))
        return True
    S.on_identify = refuse
    c = Client([])
    try:
        await wait_for(lambda: len(S.of("identify")) >= 10, 360, "10 logins")
        ids = [e[0] for e in S.of("identify")]
        gaps = [b - a for a, b in zip(ids, ids[1:])]
        print("  gaps: " + ", ".join("%.1f" % g for g in gaps), flush=True)
        check(all(g >= 0.9 for g in gaps), "never sooner than 1 s apart")
        check(gaps[-1] >= 25, "growing: %.1f s by the last" % gaps[-1])
        await asyncio.sleep(75)
        check(len(S.of("identify")) == 10, "no 11th login (stopped)")
        check("session closed (5001)" in c.output(), "told the user (session closed 5001)")
    finally:
        c.stop()


async def t_api_429_retry():
    """a 429 with retry_after: the request is made again once that passed"""
    S.api["/api/v9/gateway"] = [(429, {}, {"message": "slow down", "retry_after": 1.5, "global": False}),
                               (200, {}, {"url": "wss://localhost:%d" % GW_PORT})]
    c = Client([])
    try:
        await wait_for(lambda: S.of("connect"), 10, "the gateway")
        reqs = [e[0] for e in S.of("api") if e[2]["path"] == "/api/v9/gateway"]
        check(len(reqs) == 2, "asked twice")
        check(reqs[1] - reqs[0] >= 1.5, "the second %.2f s after the first (retry_after 1.5)" % (reqs[1] - reqs[0]))
    finally:
        c.stop()


async def t_api_bucket():
    """X-RateLimit-Remaining 0: the next request on that route waits for the reset"""
    S.api["/api/v9/test/limited"] = [(200, {"X-RateLimit-Bucket": "b1", "X-RateLimit-Remaining": "0",
                                            "X-RateLimit-Reset-After": "2.0", "X-RateLimit-Limit": "1"}, {})]
    c = Client(["--get", "https://localhost:%d/api/v9/test/limited" % API_PORT, "--times", "2"])
    try:
        await wait_for(lambda: len(S.of("api")) >= 2, 10, "two requests")
        t = [e[0] for e in S.of("api")]
        check(t[1] - t[0] >= 2.0, "the second %.2f s after the first (reset after 2 s)" % (t[1] - t[0]))
    finally:
        c.stop()


async def t_api_cloudflare_429():
    """Cloudflare's 429 (no JSON, Retry-After): every request waits, twice at most"""
    S.api["/api/v9/test/cf"] = [(429, {"Content-Type": "text/html", "Retry-After": "2"}, "<html>slow down</html>")]
    c = Client(["--get", "https://localhost:%d/api/v9/test/cf" % API_PORT])
    try:
        await wait_for(lambda: c.p.poll() is not None, 15, "dm-cli to give up")
        t = [e[0] for e in S.of("api")]
        check(len(t) == 3, "asked 3 times (the request, 2 retries)")
        check(all(b - a >= 2.0 for a, b in zip(t, t[1:])), "2 s apart at least")
    finally:
        c.stop()


async def t_api_refused_token():
    """401: the token is not sent again"""
    S.api["/api/v9/test/401"] = [(401, {}, {"message": "401: Unauthorized", "code": 0})]
    c = Client(["--get", "https://localhost:%d/api/v9/test/401" % API_PORT, "--times", "3"])
    try:
        # (the requests after the refusal are dropped, not answered: dm-cli
        # waits on, and the API sees no more)
        await wait_for(lambda: S.of("api"), 10, "the first request")
        await asyncio.sleep(4)
        check(len(S.of("api")) == 1, "one request reached the API of 3 (%d)" % len(S.of("api")))
        check(c.output().count("* HTTP 401") == 1, "the client saw the one refusal")
    finally:
        c.stop()


TESTS = [t_heartbeats, t_resume_after_close, t_resume_after_reconnect_op, t_invalid_session,
         t_dead_connection, t_heartbeat_request, t_auth_failed, t_api_429_retry, t_api_bucket,
         t_api_cloudflare_429, t_api_refused_token, t_rate_limited_close, t_login_cap]


async def main():
    sslctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    sslctx.load_cert_chain(OPTS.cert, OPTS.key)
    api = http.server.ThreadingHTTPServer(("127.0.0.1", API_PORT), Api)
    api.socket = sslctx.wrap_socket(api.socket, server_side=True)
    threading.Thread(target=api.serve_forever, daemon=True).start()
    async with serve(gateway, "127.0.0.1", GW_PORT, ssl=sslctx):
        chosen = [t for t in TESTS if not OPTS.tests or t.__name__[2:] in OPTS.tests]
        failed = []
        for t in chosen:
            S.reset()
            print("== %s: %s" % (t.__name__[2:], t.__doc__.split("\n")[0]), flush=True)
            try:
                await t()
            except AssertionError as e:
                print("  FAILED: %s" % e, flush=True)
                failed.append(t.__name__[2:])
            await asyncio.sleep(1)
        print("\n%d of %d passed%s" % (len(chosen) - len(failed), len(chosen),
                                      (": failed " + ", ".join(failed)) if failed else ""), flush=True)
        return 1 if failed else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--client"); ap.add_argument("--ca"); ap.add_argument("--cert"); ap.add_argument("--key")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("tests", nargs="*")
    OPTS = ap.parse_args()
    if OPTS.list:
        for t in TESTS:
            print("%-22s %s" % (t.__name__[2:], t.__doc__.split("\n")[0]))
        sys.exit(0)
    sys.exit(asyncio.run(main()))
