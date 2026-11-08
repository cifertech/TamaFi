"""USB line protocol client for TamaFi Face."""

from __future__ import annotations

import os
import sys
import time
from typing import Optional

import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A
BAUD = 115200
PROTO_TAG = "PONG TAMAFI"


class LinkError(Exception):
    pass


def native_port(port: str) -> str:
    p = (port or "").strip()
    if os.name == "nt" and p.upper().startswith("COM") and not p.startswith("\\\\.\\"):
        return "\\\\.\\" + p
    return p


def _score_port(vid, desc):
    text = str(desc or "")
    if vid == ESPRESSIF_VID:
        return 2
    low = text.lower()
    if any(tok in low for tok in ("usb serial", "usb-serial", "ch340", "cp210", "cdc", "serial")):
        return 1
    if str(text).upper().startswith("COM"):
        return 1
    return 0


def _row(device, desc="", vid=0, pid=0, score=None):
    device = str(device or "").strip()
    desc = str(desc or device)
    vid = int(vid or 0)
    pid = int(pid or 0)
    if score is None:
        score = _score_port(vid, desc)
    return {
        "device": device,
        "desc": desc,
        "vid": vid,
        "pid": pid,
        "score": score,
        "label": "%s  %s" % (device, desc),
    }


def _ports_from_pyserial():
    rows = []
    try:
        found = list(list_ports.comports())
    except Exception:
        return rows
    for p in found:
        try:
            device = p.device
            if not device:
                continue
            vid = p.vid if p.vid is not None else 0
            pid = p.pid if p.pid is not None else 0
            desc = p.description or device
            rows.append(_row(device, desc, vid, pid))
        except Exception:
            continue
    return rows


def _ports_from_registry():
    if sys.platform != "win32":
        return []
    try:
        import winreg
    except ImportError:
        return []
    rows = []
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM")
    except OSError:
        return rows
    try:
        i = 0
        while True:
            try:
                name, value, _ = winreg.EnumValue(key, i)
            except OSError:
                break
            i += 1
            device = str(value or "").strip()
            if not device:
                continue
            rows.append(_row(device, name or device, 0, 0, 1))
    finally:
        try:
            winreg.CloseKey(key)
        except Exception:
            pass
    return rows


def list_tamafi_ports():
    rows = _ports_from_pyserial()
    have = {row["device"].upper() for row in rows}
    for extra in _ports_from_registry():
        if extra["device"].upper() not in have:
            rows.append(extra)
            have.add(extra["device"].upper())
    rows.sort(key=lambda r: (-r["score"], r["device"]))
    return rows


def parse_kv(line: str, prefix: str) -> dict:
    out = {}
    if not line.startswith(prefix):
        return out
    for tok in line.split()[1:]:
        if "=" not in tok:
            continue
        key, val = tok.split("=", 1)
        if key == "stamps" and "/" in val:
            a, b = val.split("/", 1)
            out["stamps"] = _num(a)
            out["stamps_max"] = _num(b)
        else:
            out[key] = _num(val)
    return out


def parse_status(line: str) -> dict:
    return parse_kv(line, "STATUS")


def parse_info(line: str) -> dict:
    return parse_kv(line, "INFO")


def pick_port(rows, preferred=""):
    rows = list(rows or [])
    if preferred:
        want = preferred.strip().upper()
        for row in rows:
            if str(row.get("device") or "").strip().upper() == want:
                return row["device"]
    for row in rows:
        if row.get("score", 0) >= 2:
            return row["device"]
    scored = [row for row in rows if row.get("score", 0) >= 1]
    if len(scored) == 1:
        return scored[0]["device"]
    if len(rows) == 1:
        return rows[0]["device"]
    return ""


def parse_stamp(line: str):
    parts = line.split(None, 3)
    if len(parts) < 3:
        return None
    sid = _num(parts[1])
    have = _num(parts[2])
    title = parts[3] if len(parts) > 3 else ""
    return {"id": sid, "have": int(have) != 0, "title": title}


def parse_log(line: str):
    parts = line.split(None, 3)
    if len(parts) < 3:
        return None
    return {
        "ms": _num(parts[1]),
        "kind": parts[2],
        "text": parts[3] if len(parts) > 3 else "",
    }


def _num(val):
    try:
        if isinstance(val, (int, float)):
            return int(val)
        s = str(val)
        if s.startswith("-") and s[1:].isdigit():
            return int(s)
        if s.isdigit():
            return int(s)
        return s
    except Exception:
        return val


class TamafiLink:
    def __init__(self):
        self.ser: Optional[serial.Serial] = None
        self.port = ""

    @property
    def open(self) -> bool:
        return bool(self.ser and self.ser.is_open)

    def _open_port(self, port: str):
        self.close()
        shown = (port or "").strip()
        name = native_port(shown)
        last = None
        for attempt in range(8):
            ser = serial.Serial()
            ser.port = name
            ser.baudrate = BAUD
            ser.timeout = 0.25
            ser.write_timeout = 0
            ser.dsrdtr = False
            ser.rtscts = False
            ser.dtr = False
            ser.rts = False
            try:
                ser.open()
                self.ser = ser
                self.port = shown or name
                return
            except Exception as exc:
                last = exc
                try:
                    ser.close()
                except Exception:
                    pass
                msg = str(exc).lower()
                if "access is denied" in msg or "permission" in msg or "file specified" in msg:
                    time.sleep(0.5 + attempt * 0.25)
                    continue
                break
        text = str(last or "could not open port")
        if "access is denied" in text.lower() or "permission" in text.lower():
            raise LinkError(
                "%s is in use. Close Serial Monitor and other TamaFi Lab windows." % shown
            )
        raise LinkError("%s: %s" % (shown, text))

    def _usb_busy(self, exc) -> bool:
        msg = str(exc).lower()
        return any(
            tok in msg
            for tok in (
                "write timeout",
                "access is denied",
                "permission",
                "clearcommerror",
                "busy",
            )
        )

    def _write_cmd(self, cmd: str):
        payload = (cmd.strip() + "\n").encode("ascii", errors="ignore")
        try:
            self.ser.write(payload)
        except serial.SerialTimeoutException as exc:
            raise LinkError("Write timeout") from exc
        except Exception as exc:
            msg = str(exc).lower()
            if "hevent" in msg or "write timeout" in msg:
                raise LinkError("Write timeout") from exc
            raise

    def _quiet(self, total=2.6):
        if not self.open:
            return
        start = time.time()
        last = start
        while time.time() - start < total:
            n = 0
            try:
                n = self.ser.in_waiting
                if n:
                    self.ser.read(n)
                    last = time.time()
            except Exception:
                pass
            if time.time() - last >= 0.45 and time.time() - start >= 1.0:
                return
            time.sleep(0.05)

    def connect(self, port: str, timeout: float = 20.0) -> str:
        last = ""
        for attempt in range(2):
            try:
                self._open_port(port)
            except Exception as exc:
                last = str(exc)
                time.sleep(1.6)
                continue
            self._quiet(2.0 if attempt == 0 else 1.2)
            deadline = time.time() + timeout
            usb_stuck = False
            while time.time() < deadline:
                try:
                    if not self.open:
                        last = "port closed"
                        break
                    lines, tail = self.command("PING", wait=1.6)
                except LinkError as exc:
                    last = str(exc)
                    if self._usb_busy(exc):
                        usb_stuck = True
                        break
                    self._drain()
                    time.sleep(0.35)
                    continue
                except (OSError, serial.SerialException, AttributeError) as exc:
                    last = str(exc)
                    usb_stuck = True
                    break
                blob = " ".join(lines + [tail])
                if PROTO_TAG in blob:
                    ident = next((ln for ln in lines if ln.startswith("PONG")), "PONG TAMAFI 1")
                    return ident
                last = blob or tail or last
            self.close()
            if not usb_stuck:
                break
            time.sleep(2.0)
        raise LinkError(last or "device is not answering")

    def close(self):
        ser = self.ser
        self.ser = None
        self.port = ""
        if not ser:
            return
        try:
            cancel = getattr(ser, "cancel_write", None)
            if callable(cancel):
                cancel()
        except Exception:
            pass
        try:
            ser.close()
        except Exception:
            pass

    def command(self, cmd: str, wait: float = 1.2):
        if not self.open:
            raise LinkError("not connected")
        self._drain()
        self._write_cmd(cmd)
        lines = []
        deadline = time.time() + wait
        while time.time() < deadline:
            raw = self.ser.readline()
            if not raw:
                continue
            try:
                line = raw.decode("utf-8", errors="replace").strip()
            except Exception:
                continue
            if not line:
                continue
            if line == "OK" or line.startswith("ERR"):
                if line.startswith("ERR"):
                    raise LinkError(line)
                return lines, line
            if line.startswith(("PONG", "STATUS", "STAMP", "LOG", "INFO")):
                lines.append(line)
                if line.startswith("PONG") and PROTO_TAG in line:
                    return lines, "OK"
        if lines:
            return lines, "OK"
        raise LinkError("timeout waiting for OK")

    def status(self) -> dict:
        lines, _ = self.command("STATUS")
        for line in lines:
            if line.startswith("STATUS"):
                return parse_status(line)
        return {}

    def stamps(self):
        lines, _ = self.command("STAMPS")
        out = []
        for line in lines:
            if line.startswith("STAMP"):
                row = parse_stamp(line)
                if row:
                    out.append(row)
        return out

    def log(self):
        lines, _ = self.command("LOG")
        out = []
        for line in lines:
            if line.startswith("LOG"):
                row = parse_log(line)
                if row:
                    out.append(row)
        return out

    def info(self) -> dict:
        lines, _ = self.command("INFO")
        for line in lines:
            if line.startswith("INFO"):
                return parse_info(line)
        return {}

    def set(self, key: str, value) -> None:
        self.command("SET %s %s" % (key, value))

    def preview(self, code: str) -> None:
        self.command("PREVIEW %s" % code)

    def key(self, name: str) -> None:
        self.command("KEY %s" % name)

    def save(self) -> None:
        self.command("SAVE")

    def _drain(self):
        if not self.open:
            return
        try:
            n = self.ser.in_waiting
            if n:
                self.ser.read(n)
        except Exception:
            pass
