#!/usr/bin/env python3
"""UTDeck: read the JavaScript console of Steam's interface through Chromium's DevTools protocol.

    adb forward tcp:8080 tcp:8080          (Steam started with -cef-enable-debugging)
    cdp-console.py [SECONDS] [PORT]        needs the websocket-client module

Attaches to every page Steam's web helper has (SharedJSContext, login window, ...), enables the
Runtime and Log domains and prints console messages, uncaught exceptions and failed requests until
SECONDS have passed or the web helper goes away. Lines that look like account data (steamid,
account, token, password, email, persona, a long number) are redacted before printing.
"""
import json
import re
import sys
import threading
import time
import urllib.request

import websocket

SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 20
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
SECRET = re.compile(r"steamid|account|token|password|passw|email|persona|\b\d{9,}\b", re.I)
end = time.time() + SECONDS
lock = threading.Lock()


def out(tag, text):
    text = " ".join(str(text).split())[:300]
    if SECRET.search(text):
        text = "(redacted: looks like account data)"
    with lock:
        print(f"{time.strftime('%H:%M:%S')} [{tag}] {text}", flush=True)


def watch(target):
    tag = target.get("title") or target.get("url", "?")[:40]
    try:
        ws = websocket.create_connection(target["webSocketDebuggerUrl"], timeout=2, suppress_origin=True)
    except Exception as e:
        out(tag, f"cannot attach: {e}")
        return
    n = 0
    for method in ("Runtime.enable", "Log.enable", "Network.enable"):
        n += 1
        ws.send(json.dumps({"id": n, "method": method}))
    out(tag, f"attached ({target.get('type')}, {target.get('url', '')[:80]})")
    while time.time() < end:
        try:
            msg = json.loads(ws.recv())
        except websocket.WebSocketTimeoutException:
            continue
        except Exception:
            out(tag, "connection closed")
            return
        m, p = msg.get("method"), msg.get("params", {})
        if m == "Runtime.consoleAPICalled":
            args = " ".join(str(a.get("value", a.get("description", a.get("type")))) for a in p.get("args", []))
            out(f"{tag}/{p.get('type')}", args)
        elif m == "Runtime.exceptionThrown":
            d = p.get("exceptionDetails", {})
            out(f"{tag}/EXCEPTION", f"{d.get('text')} {d.get('exception', {}).get('description', '')} at {d.get('url', '')}:{d.get('lineNumber')}")
        elif m == "Log.entryAdded":
            e = p.get("entry", {})
            out(f"{tag}/log-{e.get('level')}", f"{e.get('text')} {e.get('url', '')}")
        elif m == "Network.loadingFailed":
            out(f"{tag}/net-fail", f"{p.get('errorText')} {p.get('blockedReason', '')}")


seen = set()
threads = []
while time.time() < end:
    try:
        targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=2))
    except Exception:
        time.sleep(0.5)
        continue
    for t in targets:
        if t.get("id") in seen or "webSocketDebuggerUrl" not in t:
            continue
        seen.add(t["id"])
        th = threading.Thread(target=watch, args=(t,), daemon=True)
        th.start()
        threads.append(th)
    time.sleep(0.5)
