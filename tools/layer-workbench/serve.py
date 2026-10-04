#!/usr/bin/env python3
"""Serves the layer workbench: index.html, the room files written by export.sh and the fixes file.

Usage: tools/layer-workbench/serve.py [ROOMS_DIR] [--rom ROM] [--port N] [--no-browser]
  ROOMS_DIR   where export.sh wrote the .room files (default ~/sm-3ds-rooms)

GET  /api/rooms   the index of the rooms (read from the files' headers)
GET  /rooms/XXXX.room   one room file, as the exporter wrote it
GET  /api/fixes   source/sm_plane_fixes.inc, as text
POST /api/fixes   replaces it (the viewer sends the whole file)
The ROM is optional here: with --rom the area map window knows where each room sits.
"""
import argparse
import http.server
import json
import os
import struct
import sys
import threading
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
FIXES = os.path.join(ROOT, 'source', 'sm_plane_fixes.inc')
HEAD = struct.Struct('<8sHBBHHHHHHHHHHH')
AREAS = ['Crateria', 'Brinstar', 'Norfair', 'Wrecked Ship', 'Maridia', 'Tourian', 'Ceres']


def map_positions(rom_path):
    """Room header pointer -> (x, y, w, h) on the area map, read from the ROM's room headers (bank $8F:
    index, area, x, y, w, h ...; the map row is y + 1). Same scan as source/sm_map.c."""
    if not rom_path or not os.path.isfile(rom_path):
        return {}
    with open(rom_path, 'rb') as f:
        rom = f.read()
    if len(rom) % 1024 == 512:
        rom = rom[512:]
    cond = {0xE5E6, 0xE5EB, 0xE5FF, 0xE612, 0xE629, 0xE640, 0xE652, 0xE669, 0xE676}
    out = {}
    for a in range(0x8000, 0x10000 - 13):
        o = 0x78000 + (a - 0x8000)
        if o + 13 > len(rom):
            break
        area, x, y, w, h = rom[o + 1:o + 6]
        doors, c = rom[o + 9] | rom[o + 10] << 8, rom[o + 11] | rom[o + 12] << 8
        if c in cond and area < 7 and 1 <= w <= 25 and 1 <= h <= 25 and x + w <= 64 and y + h <= 32 and doors >= 0x8000:
            out[a] = (x, y, w, h)
    return out


def room_index(rooms_dir, positions=None):
    positions = positions or {}
    out = []
    for name in sorted(os.listdir(rooms_dir)):
        if not name.endswith('.room'):
            continue
        with open(os.path.join(rooms_dir, name), 'rb') as f:
            raw = f.read(HEAD.size)
        magic, header, area, _r, w, h, l2, *_ = HEAD.unpack(raw)
        if not magic.startswith(b'SMRM1'):
            continue
        out.append({'room': header, 'area': area, 'area_name': AREAS[area] if area < len(AREAS) else str(area),
                    'w': w, 'h': h, 'bg2': (l2 & 1) == 0, 'file': name,
                    'map': positions.get(header)})   # [x, y, w, h] in map cells, or null without --rom
    return out


class Handler(http.server.BaseHTTPRequestHandler):
    rooms_dir = ''
    positions = {}

    def _send(self, code, body, ctype):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split('?')[0]
        if path in ('/', '/index.html'):
            with open(os.path.join(HERE, 'index.html'), 'rb') as f:
                return self._send(200, f.read(), 'text/html; charset=utf-8')
        if path == '/api/rooms':
            return self._send(200, json.dumps(room_index(self.rooms_dir, self.positions)), 'application/json')
        if path == '/api/fixes':
            text = open(FIXES, encoding='utf-8').read() if os.path.exists(FIXES) else ''
            return self._send(200, text, 'text/plain; charset=utf-8')
        if path.startswith('/rooms/') and path.endswith('.room') and '/' not in path[7:] and '..' not in path:
            fp = os.path.join(self.rooms_dir, path[7:])
            if os.path.exists(fp):
                with open(fp, 'rb') as f:
                    return self._send(200, f.read(), 'application/octet-stream')
        self._send(404, 'not found', 'text/plain')

    def do_POST(self):
        if self.path != '/api/fixes':
            return self._send(404, 'not found', 'text/plain')
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        with open(FIXES, 'wb') as f:
            f.write(body)
        self._send(200, 'ok', 'text/plain')

    def log_message(self, *a):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rooms_dir', nargs='?', default=os.path.expanduser('~/sm-3ds-rooms'))
    ap.add_argument('--port', type=int, default=8765)
    ap.add_argument('--rom', help='the ROM, only to read where each room sits on the area map')
    ap.add_argument('--no-browser', action='store_true')
    a = ap.parse_args()
    if not os.path.isdir(a.rooms_dir):
        sys.exit('no rooms in %s: run tools/layer-workbench/export.sh ROM first' % a.rooms_dir)
    Handler.rooms_dir = a.rooms_dir
    Handler.positions = map_positions(a.rom)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', a.port), Handler)
    url = 'http://127.0.0.1:%d/' % a.port
    print('layer workbench: %s  (%d rooms in %s)' % (url, len(room_index(a.rooms_dir)), a.rooms_dir))
    if not a.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
