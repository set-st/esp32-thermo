#!/usr/bin/env python3
"""Local preview of the admin page without a device (and without personal data).

Serves the real admin page from src/web_page.h and answers the API with realistic,
anonymised data (network names, IP and MAC are placeholders). Used to make the
screenshots in docs/screenshots/:

    python3 scripts/admin_mock.py            # http://localhost:8765/#mon
    # then e.g. (Windows): msedge --headless=new --window-size=1200,1400 \
    #   --virtual-time-budget=6000 --screenshot=mon.png http://localhost:8765/#mon
"""
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAGE = re.search(r'R"HTML\((.*)\)HTML"', (ROOT / "src/web_page.h").read_text("utf-8"), re.S).group(1)
START = time.time()

SENSORS = [  # name, LCD transliteration, address, raw °C, offset, max
    ("Подача", "Podacha", "28FF641E8516039C", 58.44, 0.0, 80),
    ("Зворотка", "Zvorotka", "28FF9A2B85160311", 41.69, -0.3, 80),
    ("Бак верх", "Bak verkh", "28FF0C7385160452", 62.50, 0.7, 90),
    ("Бак низ", "Bak nyz", "28FF51D28516027A", 37.94, 0.0, 90),
]


def ds_list():
    out = []
    for i, (name, _, addr, raw, off, mx) in enumerate(SENSORS):
        t = raw + off
        out.append({"slot": i + 1, "demo": False, "name": name, "addr": addr, "ok": True,
                    "offset": off, "max": mx, "raw": raw, "temp": round(t, 2),
                    "percent": round(min(max(t / mx, 0), 1) * 100, 1)})
    return out


def lcd_lines():
    # Same layout as drawPageDs() in src/ui.cpp (20x4, no demo marker, thermometer style)
    lines = []
    for i, (_, lcd, _, raw, off, mx) in enumerate(SENSORS):
        t = raw + off
        level = round(min(max(t / mx, 0), 1) * 5)
        lines.append(f"{i + 1} {lcd:<9.9}{t:5.1f}°C {chr(0x2581 + level)}")
    return lines


UPDATE = {"current": "1.1.0", "repo": "set-st/esp32-thermo", "autoCheck": True, "checked": True,
          "checkedAgo_s": 3600, "checking": False, "latest": "v1.1.0", "available": False,
          "notes": "", "url": "https://github.com/set-st/esp32-thermo/releases/tag/v1.1.0",
          "size": 1151782, "error": "", "phase": "idle", "progress": 0, "installError": ""}


def config():
    return {
        "config": {
            "wifi": {"ssid": "MyHome", "hostname": "thermohub", "hasPass": True},
            "pins": {"sda": 21, "scl": 22, "onewire": 4, "encA": 32, "encB": 35, "encBtn": 33},
            "lcd": {"addr": 0, "cols": 20, "rows": 4, "charset": 0, "barStyle": 5,
                    "blMode": 1, "blTimeout": 300},
            "encoder": {"reverse": False, "stepsPerDetent": 4},
            "pollMs": 2000, "pressUnit": 1, "dsDemo": False, "updAuto": True,
            "ds": [{"name": d["name"], "addr": d["addr"], "offset": d["offset"], "max": d["max"]}
                   for d in ds_list()],
        },
        "info": {"version": "1.1.0", "mac": "AA:BB:CC:DD:EE:FF", "ip": "192.168.1.50",
                 "apSsid": "ThermoHub-EEFF", "apActive": False, "staConnected": True,
                 "staReason": 0, "staProblem": "", "lcdAddr": 39, "bmpAddr": 119,
                 "freeHeap": 204616, "uptime_s": 93780 + int(time.time() - START),
                 "update": {k: v for k, v in UPDATE.items() if k != "notes"}},
    }


def sensors():
    return {"demo": False, "ds18b20": ds_list(),
            "aht20": {"ok": True, "temp": 22.4, "humidity": 48.1},
            "bmp280": {"ok": True, "temp": 23.0, "pressure_hpa": 1013.6, "pressure_mmhg": 760.27},
            "age_ms": 420, "uptime_s": 93780 + int(time.time() - START),
            "wifi": {"connected": True, "rssi": -52, "ip": "192.168.1.50"}}


def display():
    return {"cols": 20, "rows": 4, "present": True, "backlight": True, "barStyle": 5,
            "blMode": 1, "blOffIn": 214, "lines": lcd_lines(),
            "enc": {"pins": {"a": 32, "b": 35, "btn": 33}, "level": {"a": 1, "b": 1, "btn": 1},
                    "pullup": {"a": True, "b": False, "btn": True}, "raw": 0, "steps": 0,
                    "edgesA": 412, "edgesB": 410, "invalid": 0, "stepsPerDetent": 4,
                    "seq": 0, "events": []}}


ROUTES = {
    "/api/sensors": sensors, "/api/config": config, "/api/display": display,
    "/api/update": lambda: UPDATE,
    "/api/scan/onewire": lambda: {"found": [{"addr": a, "slot": i + 1} for i, (_, _, a, *_) in enumerate(SENSORS)]},
    "/api/scan/i2c": lambda: {"sdaIdle": 1, "sclIdle": 1, "found": [39, 56, 119], "found10k": [39, 56, 119],
                              "aht20": True, "bmp280": True},
    "/api/scan/wifi": lambda: {"networks": [{"ssid": "MyHome", "rssi": -52, "channel": 6, "open": False},
                                            {"ssid": "Neighbour", "rssi": -71, "channel": 1, "open": False}]},
}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            return self._send(200, PAGE, "text/html")
        if path in ROUTES:
            return self._send(200, json.dumps(ROUTES[path](), ensure_ascii=False))
        self._send(404, '{"ok":false,"error":"Не знайдено"}')

    def do_POST(self):  # accept everything, change nothing
        self._send(200, json.dumps(display() if "display" in self.path else {"ok": True, "reboot": False}))

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    print(f"Admin preview on http://localhost:{port}/")
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()
