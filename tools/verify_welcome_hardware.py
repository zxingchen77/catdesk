#!/usr/bin/env python3
"""Observe a flashed device through its unchanged UART, without resetting it.

The user supplies the physical touch. Only the existing identity and real
wall-clock protocol messages are sent; no synthetic touch or date is injected.
Run with ESP-IDF's Python environment (pyserial is bundled).
"""
import argparse
import json
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    if not 15 <= args.seconds <= 600:
        parser.error("Use 15–600 seconds")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    lines = []
    started = time.monotonic()
    identified = False
    next_send = 0.0
    # Setting modem outputs before opening avoids a reset on the normal WCH
    # port. An already running Helper must be stopped before this observer.
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    try:
        with args.log.open("w", encoding="utf-8") as log:
            while time.monotonic() - started < args.seconds:
                now = time.monotonic()
                if now >= next_send:
                    command = f"CD1 TIME {int(time.time())}\n" if identified else "CD1 HELLO\n"
                    port.write(command.encode("ascii"))
                    next_send = now + 2.0
                raw = port.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                log.write(f"{now-started:.3f}s {line}\n")
                log.flush()
                lines.append(line)
                if line == "CD1 READY CATDESK 1":
                    identified = True
                    next_send = 0.0
                if "CATDESK:" in line:
                    print(line, flush=True)
    finally:
        port.close()
    text = "\n".join(lines)
    failures = [s for s in ("Guru Meditation", "Brownout", "watchdog", "panic", "RGB resync failed") if s in text]
    result = {
        "duration_seconds": args.seconds,
        "identity_confirmed": identified,
        "real_time_acks": text.count("CD1 OK TIME"),
        "waiting_observed": text.count("welcome waiting for touch"),
        "physical_touch_accepted": text.count("welcome touch accepted"),
        "home_scene_stopped": text.count("welcome home; scene stopped"),
        "alive_logs": text.count("alive frames="),
        "failure_markers": failures,
        "physical_flow_pass": "welcome waiting for touch" in text
        and text.count("welcome touch accepted") == 1
        and text.count("welcome home; scene stopped") == 1
        and not failures,
    }
    args.log.with_suffix(".json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
