"""
Serial Command Tool — send commands to the ESP32 and read back logs.

Modes:
  Monitor only:
    python serial_cmd.py <port> - <seconds>

  Single command:
    python serial_cmd.py <port> <cmd> <seconds>

  E2E sequence (cmd:delay pairs, comma-separated):
    python serial_cmd.py <port> "s:10,t:5" <total_seconds>

  Example E2E test:
    python serial_cmd.py COM8 "s:15,t:5" 30
"""

import sys
import serial
import time
import threading

PORT     = sys.argv[1] if len(sys.argv) > 1 else "COM8"
CMDS_RAW = sys.argv[2] if len(sys.argv) > 2 else "-"
DURATION = int(sys.argv[3]) if len(sys.argv) > 3 else 45
BAUD     = 115200

stop_flag = threading.Event()
boot_finished = threading.Event()

def reader(ser):
    buf = b""
    while not stop_flag.is_set():
        waiting = ser.in_waiting
        if waiting:
            buf += ser.read(waiting)
            lines = buf.split(b"\n")
            buf = lines[-1]
            for line in lines[:-1]:
                try:
                    text = line.decode("utf-8", errors="replace").rstrip()
                    print(text)
                    if ("WELCOME TO RIVOT FLASH CHARGER" in text or
                        "OCPP initialized" in text or
                        "Transaction manager started" in text or
                        "[UI_TASK] Serial input listener started" in text):
                        boot_finished.set()
                except Exception:
                    pass
        else:
            time.sleep(0.05)

def banner(msg):
    print(f"\n{'='*60}")
    print(f"  {msg}")
    print(f"{'='*60}\n")

# Parse command sequence: "s:10,t:5" → [('s', 10), ('t', 5)]
# Or single command: "s" → [('s', 0)]
# Or monitor only: "-" → []
def parse_cmds(raw):
    if raw == "-":
        return []
    if ":" in raw:
        pairs = []
        for part in raw.split(","):
            part = part.strip()
            cmd, delay = part.split(":")
            pairs.append((cmd.strip(), float(delay.strip())))
        return pairs
    else:
        return [(raw, 0)]

try:
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.setDTR(False)
    ser.setRTS(False)
    ser.open()

    t = threading.Thread(target=reader, args=(ser,), daemon=True)
    t.start()

    banner(f"E2E Test | Port={PORT} | Sequence='{CMDS_RAW}' | Total={DURATION}s")
    print(">>> Monitoring logs (3s stabilization before first command)...")
    # If ESP32 just booted, wait for the boot banner. Otherwise proceed after 3s.
    boot_finished.wait(3.0)
    time.sleep(1.0)

    cmd_sequence = parse_cmds(CMDS_RAW)

    if not cmd_sequence:
        banner("Monitor-only mode — watching logs")
        time.sleep(DURATION)
    else:
        for (cmd, delay) in cmd_sequence:
            banner(f"Sending command: '{cmd}'")
            ser.write(cmd.encode())
            ser.flush()
            if delay > 0:
                print(f">>> Waiting {delay}s before next command...")
                time.sleep(delay)

        # Wait out remaining capture time
        elapsed = sum(d for _, d in cmd_sequence)
        remaining = DURATION - elapsed - 2
        if remaining > 0:
            print(f"\n>>> Final capture window: {remaining:.0f}s")
            time.sleep(remaining)

    stop_flag.set()
    t.join(timeout=2)
    ser.close()
    banner(f"Capture complete ({DURATION}s)")

except serial.SerialException as e:
    print(f"ERROR: Could not open {PORT}: {e}")
    sys.exit(1)
