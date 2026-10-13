#!/usr/bin/env python3
"""Reproduce the passed host PTY checks without opening a physical USB device.

Run helper/build.sh first, stop any running Helper, then:
    python3 helper/test_helper.py

Python is needed only for this optional regression test, not for the Helper.
"""
import os
import pathlib
import pty
import select
import signal
import subprocess
import time

binary = str(pathlib.Path(__file__).resolve().parent / "build/CatDeskHelper")


def setup():
    master, slave = pty.openpty()
    path = os.ttyname(slave)
    os.close(slave)
    proc = subprocess.Popen([binary, "--once", "--port", path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return master, path, proc


def readlines(master, seconds, carry=b""):
    deadline = time.monotonic() + seconds
    lines = []
    while time.monotonic() < deadline:
        if select.select([master], [], [], min(.05, max(0, deadline - time.monotonic())))[0]:
            try:
                data = os.read(master, 4096)
            except OSError:
                continue
            carry += data
            while b"\n" in carry:
                line, carry = carry.split(b"\n", 1)
                lines.append(line.strip().decode(errors="replace"))
    return lines, carry


def finish(proc, master):
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
    _, stderr = proc.communicate(timeout=3)
    os.close(master)
    return stderr.decode()


master, path, proc = setup()
lines, carry = readlines(master, .3)
assert "CD1 HELLO" in lines, lines
os.write(master, b"I (13) CATDESK: boot\r\nCD1 READY OTHER 1\n")
lines, carry = readlines(master, .35, carry)
assert not any(x.startswith(("CD1 TIME", "CD1 MAC")) for x in lines), lines
os.write(master, b"CD1 READY CAT")
time.sleep(.1)
os.write(master, b"DESK 1\r\r\n")
lines, carry = readlines(master, .4, carry)
time_line = next(x for x in lines if x.startswith("CD1 TIME "))
mac_line = next(x for x in lines if x.startswith("CD1 MAC "))
assert abs(int(time_line.split()[2]) - int(time.time())) < 3, time_line
fields = list(map(int, mac_line.split()[2:]))
assert len(fields) == 4 and all(0 <= n <= 100 for n in fields) and fields[1] in (0, 1), mac_line
os.write(master, b"CD1 OK TIME\r\r\n")
time.sleep(.25)
assert proc.poll() is None, "Exited before MAC acknowledgment"
os.write(master, b"CD1 OK MAC\r\r\n")
proc.wait(timeout=3)
assert proc.returncode == 0
log = finish(proc, master)
assert "time and Mac statistics acknowledged" in log, log
print("PASS identity rejection, ESP log filtering, partial/double-CR framing, UTC/live-stat ranges, both-ACK gating")

master, path, proc = setup()
lines, carry = readlines(master, .3)
second = subprocess.run([binary, "--once", "--port", path], capture_output=True, timeout=3)
assert second.returncode == 1 and b"another CatDeskHelper" in second.stderr, second.stderr
os.write(master, b"CD1 READY CATDESK 1\n")
start = time.monotonic()
updates = []
while time.monotonic() - start < 6.8:
    lines, carry = readlines(master, .2, carry)
    for line in lines:
        if line.startswith("CD1 TIME "):
            updates.append(time.monotonic())
            os.write(master, b"CD1 OK TIME\n")
        elif line.startswith("CD1 HELLO"):
            os.write(master, b"CD1 READY CATDESK 1\n")
assert proc.poll() is None, "Missing MAC acknowledgment must never report success"
log = finish(proc, master)
assert "acknowledgment timed out" in log, log
assert len(updates) >= 3, updates
assert 1.7 < updates[1] - updates[0] < 2.3, updates
print("PASS single-instance port protection, 2-second heartbeat, separate missing-MAC ACK watchdog/reconnect")
