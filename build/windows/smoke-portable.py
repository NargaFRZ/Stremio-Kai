"""Launch a disposable copy of the packaged app twice on the Windows runner.

Exercises the real window, MPV initialization, bundled Node/WebView2 runtimes,
and existing WebView activity event path without a Discord client. This is NOT
a live Discord rendering test. The distributed package is never launched or
modified by the check, and no development dependency is added to that package.
"""
import ctypes as C
from ctypes import wintypes as W
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time


PAGE = b'''<!doctype html><meta charset="utf-8"><title>Kai runtime check</title>
<script>
let started = false;
function send(event, args) {
  chrome.webview.postMessage(JSON.stringify({type:6, method:"onEvent", args:[event,args]}));
}
function initShellComm() {
  if (started) return;
  started = true;
  send("app-ready", []);
  send("activity", ["watching","movie","Dune: Part Two","","","","","","10","100","no"]);
  setTimeout(() => send("activity", ["watching","series","Breaking Bad","3","7","One Minute","","","10","100","yes"]), 1000);
  setTimeout(() => send("activity", ["watching","series","Frieren: Beyond Journey's End","1","18","First-Class Mage Exam","","","10","100","no"]), 2000);
  setTimeout(() => { send("activity", ["clear"]); fetch("/complete"); }, 3000);
}
addEventListener("load", initShellComm);
</script><body>Kai packaged runtime check</body>'''


class Handler(BaseHTTPRequestHandler):
    complete = threading.Event()

    def do_HEAD(self):
        self.send_response(200)
        self.end_headers()

    def do_GET(self):
        if self.path == '/complete':
            self.complete.set()
        self.send_response(404 if self.path == '/no-update' else 200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)

    def log_message(self, *_):
        pass


def close_window(process_id):
    user = C.WinDLL('user32', use_last_error=True)
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    user.EnumWindows.argtypes = [callback_type, W.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    matches = []

    @callback_type
    def visit(window, _):
        owner = W.DWORD()
        user.GetWindowThreadProcessId(window, C.byref(owner))
        if owner.value == process_id:
            matches.append(window)
            user.PostMessageW(window, 0x10, 0, 0)  # Normal WM_CLOSE; Kai enables CloseOnExit.
        return True

    user.EnumWindows(visit, 0)
    if not matches:
        raise RuntimeError('The app did not create a top-level Windows window')


def main(package, destination):
    package, destination = Path(package).resolve(), Path(destination).resolve()
    app = destination / 'app'
    shutil.copytree(package, app)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    endpoint = f'http://127.0.0.1:{server.server_port}/'
    checks = []
    try:
        for cycle in (1, 2):
            Handler.complete.clear()
            log_path = destination / f'launch-{cycle}.log'
            with log_path.open('wb') as log:
                process = subprocess.Popen([
                    str(app / 'stremio.exe'), f'--webui-url={endpoint}',
                    f'--autoupdater-endpoint={endpoint}no-update'
                ], cwd=app, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 50
                    while not Handler.complete.wait(0.5):
                        if process.poll() is not None:
                            raise RuntimeError(f'Launch {cycle} exited early: {process.returncode}')
                        if time.monotonic() > deadline:
                            raise RuntimeError(f'Launch {cycle}: bundled WebView did not finish the fixture')
                    # The HTTP marker follows the asynchronous native clear event.
                    while 'Clear queued to RPC' not in log_path.read_text(errors='replace'):
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError('Native activity events did not finish')
                        time.sleep(0.1)
                    close_window(process.pid)
                    if process.wait(timeout=15) != 0:
                        raise RuntimeError(f'Launch {cycle} did not shut down normally')
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=10)
            output = log_path.read_text(encoding='utf-8', errors='replace')
            for expected in ('Node server started.', 'Using local WebView2:',
                             '[WEBVIEW]: Initializing WebView...',
                             'compact=Dune: Part Two', 'compact=Breaking Bad',
                             'One Minute (S3-E7) - Paused',
                             "compact=Frieren: Beyond Journey's End",
                             'Clear queued to RPC', 'Node server stopped.'):
                if expected not in output:
                    raise RuntimeError(f'Launch {cycle}: missing runtime evidence: {expected}')
            checks.append({'launch': cycle, 'result': 'passed',
                           'scope': 'window, MPV init, bundled Node/WebView2, activity events, clean exit'})
            print(f'Packaged Windows launch/exit {cycle}: passed')
    finally:
        server.shutdown()
        server.server_close()
    (destination / 'smoke-result.json').write_text(json.dumps({
        'checks': checks, 'discord_desktop': 'NOT TESTED; no Discord client or observer account'
    }, indent=2), encoding='utf-8')


if __name__ == '__main__':
    try:
        main(*sys.argv[1:])
    except Exception:
        for path in Path(sys.argv[2]).glob('launch-*.log'):
            print(f'{path.name}:\n{path.read_text(encoding="utf-8", errors="replace")[-12000:]}', flush=True)
        raise
