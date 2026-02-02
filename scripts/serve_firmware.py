#!/usr/bin/env python3
"""Simple HTTP server for OTA firmware hosting"""
import http.server
import socketserver
import os

PORT = 8000
FIRMWARE_DIR = "../.pio/build/charger_esp32_production"

os.chdir(FIRMWARE_DIR)

Handler = http.server.SimpleHTTPRequestHandler
Handler.extensions_map['.bin'] = 'application/octet-stream'

with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print(f"Serving firmware at http://0.0.0.0:{PORT}/firmware.bin")
    print(f"Use in SteVe: http://<YOUR_IP>:{PORT}/firmware.bin")
    httpd.serve_forever()
