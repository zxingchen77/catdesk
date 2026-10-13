#!/usr/bin/env python3
"""Test the native Helper's unplug/replug loop without a physical USB port.

Build helper/build/CatDeskHelper and stop any running Helper first, then run:
    python3 helper/test_reconnect.py

One Helper process follows a temporary symlink from PTY A to PTY B. Closing
A's master produces a real terminal HUP; replacing the symlink models a
reappearing device. Both peers require HELLO/identity before accepting current
TIME/MAC data, ACK every update, and check the configured 115200/raw/HUPCL
settings. A PTY cannot measure physical USB modem/reset lines: the reset
check also audits that the native source's sole ioctl is TIOCEXCL, and
rejects any serial command other than HELLO/TIME/MAC.
"""

from __future__ import annotations

import argparse
import errno
import os
from pathlib import Path
import pty
import re
import select
import signal
import subprocess
import tempfile
import termios
import time


HELPER_DIR = Path(__file__).resolve().parent


class FirmwarePeer:
    def __init__(self, name: str):
        self.name = name
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        self.carry = b""
        self.hello_count = 0
        self.identity_sent = False
        self.times: list[int] = []
        self.macs: list[tuple[int, ...]] = []
        self.time_received_at: list[float] = []
        self.settings_checked = False

    def close(self) -> None:
        for name in ("master", "slave"):
            fd = getattr(self, name)
            if fd is not None:
                os.close(fd)
                setattr(self, name, None)

    def receive(self) -> None:
        try:
            data = os.read(self.master, 4096)
        except OSError as error:
            if error.errno in (errno.EAGAIN, errno.EINTR):
                return
            raise
        if not data:
            return
        self.carry += data
        assert len(self.carry) <= 8192, f"{self.name}: oversized serial record"
        while b"\n" in self.carry:
            raw, self.carry = self.carry.split(b"\n", 1)
            line = raw.rstrip(b"\r").decode("ascii")
            self.handle_line(line)

    def handle_line(self, line: str) -> None:
        if line == "CD1 HELLO":
            self.hello_count += 1
            attrs = termios.tcgetattr(self.slave)
            assert not attrs[2] & termios.HUPCL, f"{self.name}: close must not toggle HUPCL"
            assert not attrs[3] & (termios.ECHO | termios.ICANON), f"{self.name}: serial is not raw"
            assert attrs[4] == attrs[5] == termios.B115200, f"{self.name}: baud must be 115200"
            self.settings_checked = True
            os.write(self.master, b"CD1 READY CATDESK 1\r\n")
            self.identity_sent = True
        elif line.startswith("CD1 TIME "):
            assert self.identity_sent, f"{self.name}: TIME arrived before re-identification"
            fields = line.split()
            assert len(fields) == 3, line
            epoch = int(fields[2])
            assert abs(epoch - int(time.time())) <= 3, f"{self.name}: stale UTC time: {line}"
            self.times.append(epoch)
            self.time_received_at.append(time.monotonic())
            os.write(self.master, b"CD1 OK TIME\n")
        elif line.startswith("CD1 MAC "):
            assert self.identity_sent, f"{self.name}: MAC arrived before re-identification"
            values = tuple(map(int, line.split()[2:]))
            assert len(values) == 4 and values[1] in (0, 1), line
            assert all(0 <= value <= 100 for value in values), line
            self.macs.append(values)
            os.write(self.master, b"CD1 OK MAC\n")
        else:
            raise AssertionError(f"{self.name}: unexpected/reset command: {line!r}")

    @property
    def complete_updates(self) -> int:
        return min(len(self.times), len(self.macs))


class HelperSession:
    def __init__(self, binary: Path, port: Path):
        self.proc = subprocess.Popen([str(binary), "--port", str(port)],
                                     stdin=subprocess.DEVNULL,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.pid = self.proc.pid
        self.log = bytearray()
        os.set_blocking(self.proc.stderr.fileno(), False)

    def collect_log(self) -> None:
        while True:
            try:
                data = os.read(self.proc.stderr.fileno(), 4096)
            except BlockingIOError:
                return
            if not data:
                return
            self.log.extend(data)
            assert len(self.log) < 65536, "Unexpected unbounded Helper diagnostics"

    def wait_updates(self, peer: FirmwarePeer, count: int, seconds: float = 8) -> None:
        deadline = time.monotonic() + seconds
        while peer.complete_updates < count and time.monotonic() < deadline:
            assert self.proc.pid == self.pid
            if self.proc.poll() is not None:
                self.collect_log()
                raise AssertionError("Helper exited before reconnect completed: " + self.log.decode(errors="replace"))
            ready, _, _ = select.select([peer.master, self.proc.stderr.fileno()], [], [], .1)
            if peer.master in ready:
                peer.receive()
            if self.proc.stderr.fileno() in ready:
                self.collect_log()
        self.collect_log()
        assert peer.complete_updates >= count, f"{peer.name}: heartbeat deadline; log={self.log.decode(errors='replace')}"
        assert self.proc.poll() is None, "The same normal-mode Helper must remain alive"
        assert peer.hello_count == 1, f"{peer.name}: unexpected reconnect/reset loop"
        assert peer.settings_checked

    def shutdown(self) -> None:
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            stdout, stderr = self.proc.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.communicate()
            raise AssertionError("Helper did not shut down cleanly after SIGTERM")
        self.log.extend(stderr)
        assert self.proc.returncode == 0, self.log.decode(errors="replace")
        assert not stdout, f"Unexpected normal-mode stdout: {stdout!r}"

    def cleanup(self) -> None:
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.communicate()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=HELPER_DIR / "build/CatDeskHelper")
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK), "Build the native Helper first: ./helper/build.sh"
    source = (HELPER_DIR / "CatDeskHelper.swift").read_text()
    ioctls = re.findall(r"\bioctl\([^,]+,\s*([A-Z_]+)", source)
    assert ioctls == ["TIOCEXCL"], f"Unexpected modem-control/reset ioctl in native source: {ioctls}"
    peers: list[FirmwarePeer] = []
    session = None
    try:
        with tempfile.TemporaryDirectory(prefix="catdesk-reconnect-") as directory:
            port = Path(directory) / "CatDeskUSB"
            first, second = FirmwarePeer("PTY A"), FirmwarePeer("PTY B")
            peers.extend((first, second))
            assert first.path != second.path
            port.symlink_to(first.path)
            session = HelperSession(binary, port)
            session.wait_updates(first, 2)
            assert 1.7 <= first.time_received_at[1] - first.time_received_at[0] <= 2.6, "A: wrong heartbeat interval"

            # Closing the master generates HUP on the Helper's existing slave
            # descriptor. The new PTY is already distinct before A is revoked.
            first.close()
            replacement = Path(directory) / "CatDeskUSB.replug"
            replacement.symlink_to(second.path)
            os.replace(replacement, port)
            assert session.proc.poll() is None and session.proc.pid == session.pid
            session.wait_updates(second, 2)
            assert second.times[0] >= first.times[-1], "Reconnected helper sent an older clock"
            assert 1.7 <= second.time_received_at[1] - second.time_received_at[0] <= 2.6, "B: wrong heartbeat interval"
            log = session.log.decode(errors="replace")
            assert log.count("searching for CatDesk USB serial") == 1, log
            assert log.count(f"connected to {port}") == 2, log
            assert "USB serial disconnected" in log, log
            assert "acknowledgment timed out" not in log, log
            assert "no CatDesk identity" not in log, log
            session.shutdown()
            print(f"PASS unplug/replug: same native Helper PID {session.pid}, two identified connections, "
                  "fresh UTC/Mac statistics and two-second heartbeats on both PTYs")
            print("PASS reset safeguards: raw 115200, HUPCL disabled, only HELLO/TIME/MAC commands, "
                  "native source uses TIOCEXCL only")
            print("PASS clean SIGTERM shutdown (exit 0); no physical USB port was opened")
    finally:
        if session is not None:
            session.cleanup()
        for peer in peers:
            peer.close()


if __name__ == "__main__":
    main()
