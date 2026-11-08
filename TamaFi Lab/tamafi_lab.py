"""TamaFi Lab. Face Dock USB companion."""

from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


def _fix_stdio():
    if os.name != "nt":
        return
    exe = (sys.executable or "").lower()
    if not getattr(sys, "frozen", False) and "pythonw" not in exe:
        return
    for name in ("stdout", "stderr"):
        if getattr(sys, name, None) is None:
            setattr(sys, name, open(os.devnull, "w", encoding="utf-8", errors="ignore"))


_fix_stdio()

from tamafi_link import LinkError, TamafiLink, list_tamafi_ports, pick_port


def _ui_dir():
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent)) / "ui"
    return Path(__file__).resolve().parent / "ui"


UI_DIR = _ui_dir()
APP_TITLE = "TamaFi Lab"
APP_W = 1080
APP_H = 720
MIME = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".ico": "image/x-icon",
}


def storage_dir():
    root = Path(os.environ.get("LOCALAPPDATA") or Path.home()) / "TamaFiLab"
    root.mkdir(parents=True, exist_ok=True)
    return root


def lab_log(msg):
    try:
        with open(storage_dir() / "lab.log", "a", encoding="utf-8") as fh:
            fh.write(time.strftime("%Y-%m-%d %H:%M:%S ") + str(msg) + "\n")
    except Exception:
        pass


def protocol_miss(exc):
    msg = str(exc).lower()
    if any(
        tok in msg
        for tok in (
            "access is denied",
            "permission",
            "busy",
            "write timeout",
            "device is not answering",
            "clearcommerror",
        )
    ):
        return False
    return "pong" in msg


class Dock:
    def __init__(self):
        self.link = TamafiLink()
        self.jobs = queue.Queue()
        self.subs = []
        self.connected = False
        self.flashing = False
        self.want_link = True
        self.bin_path = ""
        self._logseq = None
        self._stamp_n = None
        self._port_blob = ""
        self._next_scan = time.time() + 0.8
        self._last_connect_try = 0
        self.cache = {}
        self._ports = []
        self._note = ""
        self._error = ""
        self._connecting = False
        self._flashlog = []
        self.stop = threading.Event()
        self._thread = None

    def start(self):
        if self._thread and self._thread.is_alive():
            return
        self.stop.clear()
        self._thread = threading.Thread(target=self._worker, daemon=True)
        self._thread.start()

    def emit(self, event, data=None):
        if event in ("connected", "status", "stamps", "log", "info", "needs_firmware", "flashed"):
            self.cache[event] = data
        if event == "connected":
            self.cache.pop("needs_firmware", None)
            self._connecting = False
            self._error = ""
        if event == "connecting":
            self._connecting = True
            self._error = ""
        if event in ("disconnected", "dropped"):
            self.cache.pop("connected", None)
            self._connecting = False
        if event == "needs_firmware":
            self._connecting = False
        if event == "note":
            self._note = "" if data is None else str(data)
        if event == "error":
            self._error = "" if data is None else str(data)
            self._connecting = False
        if event == "flashlog":
            self._flashlog.append(str(data))
            self._flashlog = self._flashlog[-240:]
        if event == "flashed":
            self._connecting = False
        blob = json.dumps({"event": event, "data": data}, default=str)
        for q in list(self.subs):
            try:
                q.put_nowait(blob)
            except queue.Full:
                pass

    def snapshot(self):
        connected = dict(self.cache.get("connected") or {}) if self.connected else {}
        return {
            "connected": bool(self.connected),
            "flashing": bool(self.flashing),
            "connecting": bool(self._connecting),
            "need_fw": bool(self.cache.get("needs_firmware")) and not self.connected,
            "ports": list(self._ports),
            "port": self.link.port or connected.get("port") or "",
            "ident": connected.get("ident") or "",
            "status": self.cache.get("status"),
            "info": self.cache.get("info"),
            "stamps": self.cache.get("stamps"),
            "log": self.cache.get("log"),
            "note": self._note,
            "error": self._error,
            "needs_firmware": self.cache.get("needs_firmware"),
            "flashlog": "\n".join(self._flashlog),
            "flashed": self.cache.get("flashed"),
            "has_bin": bool(self.bin_path and os.path.isfile(self.bin_path)),
            "bin_name": os.path.basename(self.bin_path) if self.bin_path else "",
        }

    def remember_ports(self, rows=None):
        try:
            self._ports = list(rows if rows is not None else list_tamafi_ports())
        except Exception as exc:
            lab_log("ports: %s" % exc)
            if not self._ports:
                self._ports = []
        return self._ports

    def subscribe(self):
        q = queue.Queue(maxsize=64)
        self.subs.append(q)
        return q

    def unsubscribe(self, q):
        if q in self.subs:
            self.subs.remove(q)

    def connect(self, device):
        self.want_link = True
        self.jobs.put(("connect", device))

    def disconnect(self):
        self.want_link = False
        self.jobs.put(("disconnect", None))

    def set(self, key, value):
        self.jobs.put(("set", (str(key), value)))

    def preview(self, code):
        self.jobs.put(("preview", str(code)))

    def key(self, name):
        self.jobs.put(("key", str(name)))

    def save(self):
        self.jobs.put(("save", None))

    def flash(self, port):
        if not self.bin_path or not os.path.isfile(self.bin_path):
            self.emit("error", "Choose a .bin first")
            return False
        self.flashing = True
        self.want_link = True
        self.jobs.put(("flash", (port, self.bin_path)))
        return True

    def _emit_snapshot(self):
        self.emit("status", self.link.status())
        try:
            self.emit("info", self.link.info())
        except LinkError:
            pass
        self.emit("stamps", self.link.stamps())
        self.emit("log", self.link.log())

    def _do_connect(self, device):
        lab_log("connect %s" % device)
        self.emit("connecting", {"port": device})
        self.emit("note", "Waiting for Face on " + device)
        try:
            ident = self.link.connect(device)
        except Exception:
            self._connecting = False
            raise
        self.connected = True
        self.want_link = True
        self._connecting = False
        self._logseq = None
        self._stamp_n = None
        lab_log("linked %s %s" % (device, ident))
        self.emit("connected", {"ident": ident, "port": self.link.port})
        self._emit_snapshot()

    def _reconnect(self, preferred="", timeout=28):
        self.emit("note", "waiting for TamaFi…")
        deadline = time.time() + timeout
        while time.time() < deadline and not self.stop.is_set():
            if not self.want_link:
                return False
            rows = self.remember_ports()
            blob = json.dumps([row["device"] for row in rows])
            if blob != self._port_blob:
                self._port_blob = blob
                self.emit("ports", rows)
            pick = pick_port(rows, preferred)
            if pick:
                try:
                    time.sleep(0.9)
                    self._do_connect(pick)
                    self.emit("note", "reconnected " + pick)
                    return True
                except (LinkError, serial_error()) as exc:
                    self.link.close()
                    self.connected = False
                    self.emit("note", "retry: " + str(exc)[:90])
            time.sleep(1.0)
        rows = self.remember_ports()
        self.emit("ports", rows)
        if pick_port(rows, preferred):
            self.want_link = False
            self.emit("needs_firmware", {"msg": "Face is not answering PING after flash"})
        else:
            self.emit("note", "flash ok — plug in TamaFi")
        return False

    def _worker(self):
        poll_n = 0
        while not self.stop.is_set():
            try:
                kind, payload = self.jobs.get(timeout=0.25)
            except queue.Empty:
                if self.connected and not self.flashing:
                    kind, payload = ("poll", None)
                elif not self.connected and not self.flashing:
                    kind, payload = ("scan", None)
                else:
                    kind, payload = (None, None)
            if kind is None:
                continue
            try:
                if kind == "connect":
                    device = payload or pick_port(self.remember_ports(), "")
                    if not device:
                        raise LinkError("no TamaFi on USB")
                    self._do_connect(device)
                elif kind == "disconnect":
                    self.connected = False
                    self.link.close()
                    self.emit("disconnected", "disconnected")
                elif kind == "scan":
                    now = time.time()
                    if now < self._next_scan:
                        continue
                    self._next_scan = now + 2.5
                    rows = self.remember_ports()
                    blob = json.dumps([row["device"] for row in rows])
                    if blob != self._port_blob:
                        self._port_blob = blob
                        self.emit("ports", rows)
                    if not self.want_link or now - self._last_connect_try < 14:
                        continue
                    pick = pick_port(rows, "")
                    if not pick:
                        continue
                    self._last_connect_try = now
                    self._do_connect(pick)
                elif kind == "poll" and self.connected:
                    st = self.link.status()
                    self.emit("status", st)
                    stamps_n = st.get("stamps")
                    logseq = st.get("logseq")
                    if stamps_n != self._stamp_n:
                        self._stamp_n = stamps_n
                        self.emit("stamps", self.link.stamps())
                    if logseq != self._logseq:
                        self._logseq = logseq
                        self.emit("log", self.link.log())
                    poll_n += 1
                    if poll_n % 8 == 0:
                        self.emit("stamps", self.link.stamps())
                        try:
                            self.emit("info", self.link.info())
                        except LinkError:
                            pass
                elif kind == "set" and self.connected:
                    key, value = payload
                    self.link.set(key, value)
                    self.emit("status", self.link.status())
                    self.emit("note", "set %s %s" % (key, value))
                elif kind == "preview" and self.connected:
                    self.link.preview(payload)
                    self.emit("note", "preview %s" % payload)
                elif kind == "key" and self.connected:
                    self.link.key(payload)
                    try:
                        self.emit("status", self.link.status())
                    except LinkError:
                        pass
                    self.emit("note", "key " + payload)
                elif kind == "save" and self.connected:
                    self.link.save()
                    self.emit("note", "saved")
                elif kind == "flash":
                    port, path = payload
                    self.connected = False
                    self.link.close()
                    self.emit("disconnected", "flashing")
                    ok, msg = run_flash(
                        port,
                        path,
                        lambda line: (self.emit("flashlog", line), self.emit("note", line[-120:])),
                    )
                    self.flashing = False
                    self.emit("flashed", {"ok": ok, "msg": msg})
                    self.emit("ports", self.remember_ports())
                    if ok:
                        self.want_link = True
                        self._reconnect(port)
            except (LinkError, serial_error()) as exc:
                lab_log("fail %s: %s" % (kind, exc))
                self._connecting = False
                if kind in ("poll", "set", "preview", "key", "save") and self.connected:
                    self.connected = False
                    self.link.close()
                    self.emit("dropped", str(exc))
                elif kind == "scan":
                    self.link.close()
                    self.connected = False
                    self._last_connect_try = time.time()
                    self._next_scan = time.time() + 12
                    if protocol_miss(exc):
                        self.want_link = False
                        self.emit("needs_firmware", {"msg": str(exc)})
                    else:
                        self.emit("error", str(exc)[:160])
                        self.emit("disconnected", str(exc)[:160])
                else:
                    if kind == "connect" and protocol_miss(exc):
                        self.want_link = False
                        self.link.close()
                        self.emit("needs_firmware", {"msg": str(exc)})
                    else:
                        self.emit("error", str(exc))
                        if kind == "connect":
                            self.link.close()
                            self.emit("disconnected", str(exc))
                        if kind == "flash":
                            self.flashing = False
                            self.emit("flashed", {"ok": False, "msg": str(exc)})
            except Exception as exc:
                lab_log("worker %s: %s" % (kind, exc))
                self._connecting = False
                self.emit("error", str(exc)[:120])


class LabApi:
    def snapshot(self):
        try:
            return DOCK.snapshot()
        except Exception as exc:
            lab_log("snapshot: %s" % exc)
            return {"connected": False, "ports": [], "error": str(exc)}

    def ports(self):
        try:
            return DOCK.remember_ports()
        except Exception as exc:
            lab_log("ports: %s" % exc)
            return []

    def connect(self, device=""):
        DOCK.connect(device or "")
        return {"ok": True}

    def disconnect(self):
        DOCK.disconnect()
        return {"ok": True}

    def set_value(self, key, value):
        DOCK.set(key, value)
        return {"ok": True}

    def preview(self, code):
        DOCK.preview(code)
        return {"ok": True}

    def key(self, name):
        DOCK.key(name or "")
        return {"ok": True}

    def save(self):
        DOCK.save()
        return {"ok": True}

    def pick_firmware(self):
        chosen = pick_bin_dialog()
        if chosen:
            DOCK.bin_path = chosen
            return {"ok": True, "name": os.path.basename(chosen)}
        return {"ok": False, "name": ""}

    def flash(self, port=""):
        ok = DOCK.flash(port or "")
        return {"ok": bool(ok)}


DOCK = Dock()


def serial_error():
    try:
        import serial

        return serial.SerialException
    except Exception:
        return OSError


def esptool_prefix():
    if getattr(sys, "frozen", False):
        return ["py", "-3", "-m", "esptool"]
    return [sys.executable, "-m", "esptool"]


def run_flash(port, path, on_line):
    name = os.path.basename(path).lower()
    offset = "0x0" if "merged" in name else "0x10000"
    cmd = esptool_prefix() + [
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        "921600",
        "write_flash",
        offset,
        path,
    ]
    on_line(" ".join(cmd))
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1
        )
    except OSError as exc:
        return False, "could not start esptool: %s\nInstall with: py -3 -m pip install esptool" % exc
    lines = []
    for raw in proc.stdout:
        line = raw.rstrip()
        lines.append(line)
        on_line(line[-200:])
    rc = proc.wait()
    text = "\n".join(lines[-20:])
    if rc != 0:
        return False, text or "esptool exit %s" % rc
    return True, "flash ok at %s" % offset


def pick_bin_dialog():
    ps = (
        "Add-Type -AssemblyName System.Windows.Forms; "
        "$d = New-Object System.Windows.Forms.OpenFileDialog; "
        "$d.Filter = 'Binary (*.bin)|*.bin|All (*.*)|*.*'; "
        "$d.Title = 'TamaFi firmware'; "
        "if ($d.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $d.FileName }"
    )
    try:
        r = subprocess.run(
            ["powershell", "-NoProfile", "-STA", "-Command", ps],
            capture_output=True,
            text=True,
            timeout=300,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    except Exception:
        return ""
    return (r.stdout or "").strip()


def read_json(handler):
    n = int(handler.headers.get("Content-Length") or 0)
    if n <= 0:
        return {}
    raw = handler.rfile.read(n)
    try:
        return json.loads(raw.decode("utf-8") or "{}")
    except Exception:
        return {}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        return

    def _send(self, code, body, mime="application/json; charset=utf-8"):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, default=str))

    def do_GET(self):
        path = unquote(urlparse(self.path).path)
        if path == "/api/events":
            self._sse()
            return
        if path == "/api/state":
            self._json(DOCK.snapshot())
            return
        if path == "/api/ports":
            self._json(DOCK.remember_ports())
            return
        if path == "/":
            path = "/index.html"
        rel = path.lstrip("/").replace("\\", "/")
        if ".." in rel.split("/"):
            self._send(403, "no")
            return
        fp = (UI_DIR / rel).resolve()
        if not str(fp).startswith(str(UI_DIR.resolve())) or not fp.is_file():
            self._send(404, "missing")
            return
        self._send(200, fp.read_bytes(), MIME.get(fp.suffix.lower(), "application/octet-stream"))

    def do_POST(self):
        path = urlparse(self.path).path
        body = read_json(self)
        if path == "/api/connect":
            DOCK.connect(body.get("device") or "")
            self._json({"ok": True})
        elif path == "/api/disconnect":
            DOCK.disconnect()
            self._json({"ok": True})
        elif path == "/api/set":
            DOCK.set(body.get("key"), body.get("value"))
            self._json({"ok": True})
        elif path == "/api/preview":
            DOCK.preview(body.get("code"))
            self._json({"ok": True})
        elif path == "/api/key":
            DOCK.key(body.get("name") or "")
            self._json({"ok": True})
        elif path == "/api/save":
            DOCK.save()
            self._json({"ok": True})
        elif path == "/api/pick_firmware":
            chosen = pick_bin_dialog()
            if chosen:
                DOCK.bin_path = chosen
                self._json({"ok": True, "name": os.path.basename(chosen)})
            else:
                self._json({"ok": False, "name": ""})
        elif path == "/api/flash":
            ok = DOCK.flash(body.get("port") or "")
            self._json({"ok": bool(ok)})
        else:
            self._json({"ok": False, "error": "unknown"}, 404)

    def _sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        q = DOCK.subscribe()
        try:
            hello = [{"event": "ports", "data": DOCK.remember_ports()}]
            if DOCK.connected and DOCK.cache.get("connected"):
                hello.append({"event": "connected", "data": DOCK.cache["connected"]})
                for ev in ("status", "info", "stamps", "log"):
                    if ev in DOCK.cache:
                        hello.append({"event": ev, "data": DOCK.cache[ev]})
            elif DOCK.cache.get("needs_firmware"):
                hello.append({"event": "needs_firmware", "data": DOCK.cache["needs_firmware"]})
            for msg in hello:
                self.wfile.write(b"data: " + json.dumps(msg, default=str).encode("utf-8") + b"\n\n")
            self.wfile.flush()
            while not DOCK.stop.is_set():
                try:
                    msg = q.get(timeout=12)
                    self.wfile.write(b"data: " + msg.encode("utf-8") + b"\n\n")
                    self.wfile.flush()
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        finally:
            DOCK.unsubscribe(q)


def _bgr(hex_rgb):
    h = hex_rgb.replace("#", "")
    r = int(h[0:2], 16)
    g = int(h[2:4], 16)
    b = int(h[4:6], 16)
    return r | (g << 8) | (b << 16)


def apply_caption_theme(hwnd):
    import ctypes
    from ctypes import wintypes

    if not hwnd:
        return
    try:
        dwmapi = ctypes.windll.dwmapi
        dwmapi.DwmSetWindowAttribute.argtypes = [
            wintypes.HWND,
            ctypes.c_uint,
            ctypes.c_void_p,
            ctypes.c_uint,
        ]
        dwmapi.DwmSetWindowAttribute.restype = ctypes.c_long
    except Exception:
        return

    attrs = {
        20: ctypes.c_int(1),
        34: ctypes.c_uint(_bgr("3d6774")),
        35: ctypes.c_uint(_bgr("1e3a44")),
        36: ctypes.c_uint(_bgr("8fb3b8")),
    }
    for attr, value in attrs.items():
        try:
            dwmapi.DwmSetWindowAttribute(hwnd, attr, ctypes.byref(value), ctypes.sizeof(value))
        except Exception:
            pass


def apply_window_icon(hwnd):
    import ctypes

    ico = UI_DIR / "brand" / "app.ico"
    if not hwnd or not ico.is_file():
        return
    IMAGE_ICON = 1
    LR_LOADFROMFILE = 0x0010
    LR_DEFAULTSIZE = 0x0040
    WM_SETICON = 0x0080
    handle = ctypes.windll.user32.LoadImageW(
        None, str(ico), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE
    )
    if handle:
        ctypes.windll.user32.SendMessageW(hwnd, WM_SETICON, 1, handle)
        ctypes.windll.user32.SendMessageW(hwnd, WM_SETICON, 0, handle)


def native_hwnd(window):
    native = getattr(window, "native", None)
    if native is None:
        return 0
    for attr in ("Handle", "handle"):
        try:
            value = getattr(native, attr, 0)
            if value:
                return int(value)
        except Exception:
            pass
    try:
        return int(native)
    except Exception:
        return 0


def hide_console():
    if os.name != "nt" or os.environ.get("TAMAFILAB_DEBUG"):
        return
    import ctypes

    hwnd = ctypes.windll.kernel32.GetConsoleWindow()
    if hwnd:
        ctypes.windll.user32.ShowWindow(hwnd, 0)


def set_app_id():
    if os.name != "nt":
        return
    import ctypes

    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("CiferTech.TamaFiLab")
    except Exception:
        pass


def alert(msg):
    if os.name == "nt":
        import ctypes

        ctypes.windll.user32.MessageBoxW(None, str(msg), APP_TITLE, 0x10)
    else:
        sys.stderr.write(str(msg) + "\n")


_MUTEX = None


def _lab_pids_to_stop():
    me = os.getpid()
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        raw = subprocess.check_output(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                "Get-CimInstance Win32_Process | "
                "Where-Object { $_.Name -match '^(TamaFiLab|pythonw|python|pyw)\\.exe$' } | "
                "Select-Object ProcessId,Name,CommandLine | ConvertTo-Json -Compress",
            ],
            text=True,
            timeout=10,
            creationflags=flags,
        )
    except Exception as exc:
        lab_log("pid scan: %s" % exc)
        return []
    raw = (raw or "").strip()
    if not raw:
        return []
    try:
        data = json.loads(raw)
    except Exception:
        return []
    if isinstance(data, dict):
        data = [data]
    pids = []
    for row in data:
        try:
            pid = int(row.get("ProcessId") or 0)
        except (TypeError, ValueError):
            continue
        if pid <= 0 or pid == me:
            continue
        name = str(row.get("Name") or "")
        cmd = str(row.get("CommandLine") or "")
        if name.lower().startswith("tamafi"):
            pids.append(pid)
        elif "tamafi_lab.py" in cmd.lower():
            pids.append(pid)
    return pids


def stop_other_labs():
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    for pid in _lab_pids_to_stop():
        lab_log("stop leftover pid %s" % pid)
        try:
            subprocess.run(
                ["taskkill", "/F", "/PID", str(pid)],
                capture_output=True,
                timeout=6,
                creationflags=flags,
            )
        except Exception as exc:
            lab_log("stop pid %s: %s" % (pid, exc))
    if _lab_pids_to_stop():
        time.sleep(0.8)


def take_lab_mutex():
    global _MUTEX
    if os.name != "nt":
        return True
    import ctypes

    kernel32 = ctypes.windll.kernel32
    _MUTEX = kernel32.CreateMutexW(None, True, "Local\\CiferTech.TamaFiLab")
    if kernel32.GetLastError() == 183:
        try:
            hwnd = ctypes.windll.user32.FindWindowW(None, APP_TITLE)
            if hwnd:
                ctypes.windll.user32.ShowWindow(hwnd, 9)
                ctypes.windll.user32.SetForegroundWindow(hwnd)
        except Exception:
            pass
        return False
    return True


def shutdown_host(httpd):
    DOCK.stop.set()
    try:
        DOCK.link.close()
    except Exception:
        pass
    try:
        httpd.shutdown()
    except Exception:
        pass


def open_app_window(url):
    try:
        import webview
    except ImportError:
        alert("TamaFi Lab needs pywebview.\n\npy -3 -m pip install pywebview")
        return False

    window = webview.create_window(
        APP_TITLE,
        url,
        width=APP_W,
        height=APP_H,
        min_size=(900, 620),
        background_color="#1e3a44",
        text_select=False,
        zoomable=False,
        js_api=LabApi(),
    )

    def decorate():
        hwnd = native_hwnd(window)
        apply_caption_theme(hwnd)
        apply_window_icon(hwnd)

    try:
        window.events.shown += decorate
    except Exception:
        pass

    kwargs = {
        "private_mode": False,
        "storage_path": str(storage_dir() / "webview"),
        "debug": bool(os.environ.get("TAMAFILAB_DEBUG")),
    }
    ico = UI_DIR / "brand" / "app.ico"
    if ico.is_file():
        kwargs["icon"] = str(ico)
    if os.name == "nt":
        kwargs["gui"] = "edgechromium"
    try:
        webview.start(**kwargs)
    except TypeError:
        kwargs.pop("gui", None)
        webview.start(**kwargs)
    except Exception as exc:
        alert("Could not open TamaFi Lab window:\n%s" % exc)
        return False
    return True


def main():
    hide_console()
    set_app_id()
    try:
        import serial  # noqa: F401
    except ImportError:
        alert("Install pyserial first:\npy -3 -m pip install pyserial")
        return

    stop_other_labs()
    time.sleep(0.6)
    if not take_lab_mutex():
        return
    DOCK.start()

    httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    httpd.daemon_threads = True
    host, port = httpd.server_address
    url = "http://127.0.0.1:%s/" % port
    lab_log("listen %s" % url)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        open_app_window(url)
    finally:
        shutdown_host(httpd)


if __name__ == "__main__":
    main()


