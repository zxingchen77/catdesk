#!/usr/bin/env python3
"""Bounded real USB integration check. Stop any existing Helper first.

Uses the native Helper's identity-gated transport and real Mac/time data.
Does not flash, reset, change pins, or send simulated anniversary dates.
"""
import argparse
import pathlib
import signal
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=44)
    args = parser.parse_args()
    if args.seconds < 10:
        parser.error('Use at least 10 seconds to check repeated updates.')
    binary = ROOT / 'helper/build/CatDeskHelper'
    if not binary.exists():
        subprocess.run([str(ROOT / 'helper/build.sh')], check=True)
    log = ROOT / 'logs/helper-soak.log'
    log.parent.mkdir(exist_ok=True)
    with log.open('w') as stream:
        process = subprocess.Popen([str(binary), '--trace'], stdout=stream, stderr=stream)
        try:
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError('Helper exited; check the log and stop any other running Helper.')
                time.sleep(0.5)
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    text = log.read_text()
    time_sent = text.count('TX "CD1 TIME')
    mac_sent = text.count('TX "CD1 MAC')
    time_acks = text.count('RX raw "CD1 OK TIME')
    mac_acks = text.count('RX raw "CD1 OK MAC')
    assert process.returncode == 0, text
    assert 'timed out' not in text and 'reconnecting' not in text, text
    assert time_sent >= 3 and time_sent == time_acks and mac_sent == mac_acks, text
    result = (f'Physical Helper {args.seconds}-second check PASS: '
              f'{time_acks}/{time_sent} TIME ACKs, {mac_acks}/{mac_sent} MAC ACKs; '
              'no timeouts or reconnects.\n')
    (ROOT / 'logs/helper-soak-result.txt').write_text(result)
    print(result, end='')


if __name__ == '__main__':
    main()
