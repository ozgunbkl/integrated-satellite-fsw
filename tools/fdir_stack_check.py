"""Bounded real-board archive-full/FDIR stack check; --reset loses the RAM archive."""
import argparse
import re
import time

from watchdog_system_check import (
    WatchdogEvidence, panic_diagnostic, BOOT, CORE, RESOURCES, ARM, SOFTWARE, NOMINAL,
)

STACK_PREFIX = "FDIR: stack_free_min_bytes="
STACK = re.compile(r"^FDIR: stack_free_min_bytes=(\d+)\.$")
FULL = re.compile(r"FDIR: Fault from Source 3 \(Severity: 1\) Code: 0xAF01\b")
HANDLED = "FDIR: Warning threshold handled for source 3; DEGRADED decision committed."


class FDIRStackEvidence:
    def __init__(self, duration=300):
        if type(duration) is not int or not 300 <= duration <= 600:
            raise ValueError("Observe 300..600 seconds after supervisor startup")
        self.base = WatchdogEvidence("normal", duration)
        self.full_count = 0
        self.handled_at = self.stack_at = self.stack_min = None

    def observe(self, line, now):
        line = re.sub(r"\x1b\[[0-9;]*m", "", line).strip()
        if panic_diagnostic(line) or "brownout" in line.lower():
            raise RuntimeError("Unexpected SDK panic/stack overflow/brownout")
        self.base.observe(line, now)
        if FULL.search(line):
            self.full_count += 1
        if line == HANDLED:
            if self.full_count < 3:
                raise RuntimeError("Warning batch lacks archive-full evidence")
            if self.handled_at is None:
                self.handled_at = now
        if line.startswith(STACK_PREFIX):
            match = STACK.fullmatch(line)
            if not match or self.base.software_time is None:
                raise RuntimeError("Malformed/out-of-order FDIR stack report")
            remaining = int(match[1])
            if not 512 <= remaining <= 4096:
                raise RuntimeError("FDIR stack margin below 512 bytes or outside budget")
            if self.stack_min is not None and remaining > self.stack_min:
                raise RuntimeError("FDIR lifetime minimum increased unexpectedly")
            self.stack_min, self.stack_at = remaining, now

    def complete(self, now):
        return (self.base.complete(now) and self.handled_at is not None and
                now - self.handled_at >= 60 and self.stack_at is not None and
                self.stack_at > self.handled_at and now - self.stack_at <= 35)

    def summary(self):
        return (f"single boot; {self.full_count} archive-full warnings; DEGRADED batch handled; "
                f"FDIR minimum free stack {self.stack_min} bytes; no panic/reset/critical fault")


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--duration", type=int, default=300)
    parser.add_argument("--reset", action="store_true", required=True,
                        help="Restart normal firmware; existing RAM archive is lost")
    args = parser.parse_args()
    evidence = FDIRStackEvidence(args.duration)
    with serial.Serial() as port:
        port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
        port.dtr = port.rts = False
        port.open()
        port.reset_input_buffer()
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        started = time.monotonic()
        while time.monotonic() - started < args.duration + 60:
            line = port.readline().decode("utf-8", errors="replace").strip()
            line = re.sub(r"\x1b\[[0-9;]*m", "", line)
            detail = panic_diagnostic(line)
            if detail:
                print(detail, flush=True)
            now = time.monotonic()
            evidence.observe(line, now)
            # Never echo arbitrary Wi-Fi/SDK output or credentials.
            if (line.startswith((BOOT, CORE, RESOURCES, ARM, SOFTWARE,
                                 "WATCHDOG: SDK recovery configured", STACK_PREFIX)) or
                    FULL.search(line) or line == HANDLED or
                    (line == NOMINAL and len(evidence.base.nominal_times) <= 3)):
                print(line, flush=True)
            if evidence.complete(now):
                print("PASS: " + evidence.summary() +
                      f" ({now - started:.1f}s observation).", flush=True)
                return
        raise RuntimeError("Bound expired without complete archive-full/FDIR stack evidence")


if __name__ == "__main__":
    main()
