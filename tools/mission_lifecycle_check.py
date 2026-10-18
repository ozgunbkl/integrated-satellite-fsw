"""Bounded EPS/service-chain reader; explicit RAM-losing reset and three NOMINAL TCs.

Uses the existing allowlisted sender (preview, fixed binary, Sent/history proof,
no retry). No fault-clear, archive operation, link/server mutation or upload.
The outer runner must preserve current RAM first and restore normal firmware.
"""
import argparse
from contextlib import ExitStack
import ipaddress
import math
from pathlib import Path
from queue import SimpleQueue
import re
import threading
import time

from resource_monitor_check import EvidenceError, SerialLines, write_snapshot
from watchdog_recovery_check import ModeSender, ACK, NACK, CRC, ROUTE
from watchdog_system_check import BOOT, CORE, SOFTWARE, CONFIG, FAULT, panic_diagnostic
from yamcs_archive_transfer import JsonHttp

PROFILE = "TEST: LIFECYCLE profile=eps_service_chain."
DEMO = "TEST: LIFECYCLE demo_commands=disabled."
CLEAR = "EPS: Verified 8.50 V recovery; low-voltage fault resolved, system mode unchanged."
STATE = re.compile(r"TEST: LIFECYCLE state=(boot|operating|faulted|cleared|ground|complete) mode=(\d+) deployment=([01]) eps=(\d+) adcs=(\d+) payload=(\d+) bytes=(\d+) faults=([01]) revision=(\d+) pdu=(\d+) archive=(\d+)\.")
REQUEST = re.compile(r"TEST: LIFECYCLE request=([123])\.")
COMMAND = re.compile(r"TEST: LIFECYCLE command=([123]) mode=([012])\.")
RETAINED = re.compile(r"TEST: LIFECYCLE retained index=(\d+) crc=(\d+) length=64\.")
STACK = re.compile(r"TEST: LIFECYCLE stack_free_min_bytes=(\d+)\.")
STAGES = ("boot", "operating", "faulted", "cleared", "ground", "complete")


class LifecycleEvidence:
    def __init__(self):
        self.boots = 0
        self.config = self.profile = self.demo = self.core = self.software = False
        self.last_now = None
        self.states, self.requests, self.commands, self.histories = [], [], [], []
        self.crcs = self.routes = self.acks = self.nacks = self.faults = self.clears = 0
        self.retained = self.finished = False
        self.stack_min = None

    def observe(self, line, now):
        if (not isinstance(now, (int, float)) or isinstance(now, bool) or not math.isfinite(now) or now < 0 or
                (self.last_now is not None and now < self.last_now)):
            raise EvidenceError("Invalid/non-monotonic observation clock")
        self.last_now = now
        if match := re.fullmatch(r"TEST ERROR: LIFECYCLE check=(\d{1,5})\.", line):
            raise EvidenceError(f"Lifecycle target check failed: {int(match[1])}")
        if line in ("TEST ERROR: LIFECYCLE unexpected command.", "TEST ERROR: LIFECYCLE command outcome."):
            raise EvidenceError("Lifecycle target rejected command identity or outcome")
        if len(line) > 1024 or panic_diagnostic(line) or any(text in line for text in
                ("TEST ERROR:", "abort()", "Brownout detector", "Task watchdog got triggered", "Interrupt wdt",
                 "Telemetry Queue Full", "command queue full, dropped", "Resource diagnostics unavailable")):
            raise EvidenceError("Unexpected test/SDK/resource diagnostic")
        if BOOT in line:
            if self.boots: raise EvidenceError("Unexpected lifecycle reboot")
            self.boots = 1
        elif line.startswith("WATCHDOG: SDK recovery configured"):
            match = CONFIG.fullmatch(line)
            if (not self.boots or self.config or not match or tuple(map(int, match.groups()[:3])) != (15000, 3, 1) or
                    not 0 <= int(match[4]) <= 15):
                raise EvidenceError("Wrong/repeated/missing SDK configuration")
            self.config = True
        elif line == SOFTWARE:
            if not self.config or self.software: raise EvidenceError("Supervisor startup order/repetition")
            self.software = True
        elif line.startswith(CORE):
            if not self.config or self.core: raise EvidenceError("Core startup order/repetition")
            self.core = True
        elif line in (PROFILE, DEMO):
            attribute = "profile" if line == PROFILE else "demo"
            if not self.software or getattr(self, attribute): raise EvidenceError("Test isolation/startup order/repetition")
            setattr(self, attribute, True)
        elif match := FAULT.search(line):
            if (tuple(map(int, match.groups()[:2])) != (1, 2) or int(match[3], 16) != 0xE102 or
                    self.faults or not self.states or self.states[-1]["stage"] != "operating"):
                raise EvidenceError("Unexpected fault identity/count/stage")
            self.faults += 1
        elif line == CLEAR:
            if self.clears or not self.states or self.states[-1]["stage"] != "faulted" or self.commands != [1, 2]:
                raise EvidenceError("Unexpected EPS owner-clear order/count")
            self.clears += 1
        elif line in (CRC, ROUTE, ACK, NACK):
            request = len(self.requests)
            if request != len(self.commands) + 1: raise EvidenceError("Unrequested ground command evidence")
            if line == CRC: self.crcs += 1
            elif line == ROUTE: self.routes += 1
            elif line == ACK:
                if request != 3: raise EvidenceError("Mode accepted before health/deployment authorization")
                self.acks += 1
            else:
                if request == 3: raise EvidenceError("Healthy ground mode request rejected")
                self.nacks += 1
        elif match := REQUEST.fullmatch(line):
            number = int(match[1])
            if (not self.states or self.states[-1]["stage"] != ("boot", "faulted", "cleared")[number-1] or
                    number != len(self.requests) + 1 or len(self.commands) != number - 1):
                raise EvidenceError("Wrong/repeated/out-of-order ground request")
            self.requests.append(number)
        elif match := COMMAND.fullmatch(line):
            number, mode = map(int, match.groups())
            if (number != len(self.commands) + 1 or number != len(self.requests) or mode != (0, 1, 2)[number-1] or
                    self.crcs != number or self.routes != number or self.nacks != min(number, 2) or self.acks != int(number == 3)):
                raise EvidenceError("Incomplete/wrong ground CRC/route/result proof")
            self.commands.append(number)
        elif match := STATE.fullmatch(line):
            if not self.profile or not self.demo: raise EvidenceError("Service state before test isolation")
            stage = match[1]
            expected = STAGES[min(len({row['stage'] for row in self.states}), 5)]
            if stage != expected: raise EvidenceError("Skipped/repeated/out-of-order lifecycle stage")
            keys = ("mode", "deployment", "eps", "adcs", "payload", "bytes", "faults", "revision", "pdu", "archive")
            row = dict(zip(keys, map(int, match.groups()[1:])), stage=stage, at=now)
            if (row["bytes"] % 64 or not 0 <= row["bytes"] <= 30*64 or not 0 <= row["archive"] < 4096 or
                    row["pdu"] > 0xFFFFFFFF or row["revision"] > 0xFFFFFFFF or
                    bool(row["faults"]) != bool(row["revision"])):
                raise EvidenceError("Invalid service counters/ledger")
            if self.states and (row["bytes"] < self.states[-1]["bytes"] or row["archive"] < self.states[-1]["archive"]):
                raise EvidenceError("Payload/archive history lost")
            values = tuple(row[key] for key in ("mode", "deployment", "eps", "adcs", "payload", "faults"))
            if stage == "boot":
                valid = values == (0, 0, 0, 1, 1, 0) and row["bytes"] == 0
            elif stage == "operating":
                valid = values == (2, 1, 0, 3, 2, 0) and row["bytes"] > 0 and self.commands == [1]
            elif stage in ("faulted", "cleared"):
                valid = values == (1, 1, 2, 0, 4, int(stage == "faulted")) and row["pdu"] & 7 == 4
                valid = valid and self.faults == 1 and self.commands == ([1] if stage == "faulted" else [1, 2])
                if stage == "cleared": valid = valid and self.clears == 1 and row["bytes"] == self.states[-1]["bytes"]
            elif stage == "ground":
                valid = values == (2, 1, 0, 0, 4, 0) and self.commands == [1, 2, 3] and row["bytes"] == self.states[-1]["bytes"]
            else:
                valid = (values[:3] == (2, 1, 0) and row["adcs"] in (1, 3) and
                         (row["payload"], row["faults"]) == (1, 0) and self.retained)
                valid = valid and row["bytes"] > next(r["bytes"] for r in self.states if r["stage"] == "ground")
                if self.states[-1]["stage"] == "complete": valid = valid and row["bytes"] == self.states[-1]["bytes"]
            if stage in ("boot", "operating", "ground", "complete"): valid = valid and row["pdu"] & 7 == 7
            if not valid: raise EvidenceError("Service permissions/recovery outcome mismatch")
            self.states.append(row)
        elif match := RETAINED.fullmatch(line):
            if (self.retained or not self.states or self.states[-1]["stage"] != "ground" or
                    not 0 <= int(match[1]) < 30 or not 0 <= int(match[2]) <= 65535):
                raise EvidenceError("Invalid/repeated/out-of-order CRC retention proof")
            self.retained = True
        elif match := STACK.fullmatch(line):
            if self.stack_min is not None or not 768 <= int(match[1]) <= 4096 or len([r for r in self.states if r['stage'] == 'complete']) != 3:
                raise EvidenceError("Invalid/repeated/premature stack evidence")
            self.stack_min = int(match[1])
        elif line == "TEST: LIFECYCLE finished.":
            final = [row for row in self.states if row["stage"] == "complete"]
            if self.finished or len(final) != 3 or final[-1]["at"] - final[0]["at"] < 1.8 or self.stack_min is None:
                raise EvidenceError("Incomplete/repeated/premature finish")
            self.finished = True
        elif line.startswith("TEST:"):
            raise EvidenceError("Malformed or unrelated test marker")

    def complete(self):
        return self.finished and self.core and self.histories == [1, 2, 3]

    def snapshot(self, now, result, reason=None):
        return dict(result=result, reason=reason, observation_seconds=round(now, 3), boots=self.boots,
                    states=self.states, requests=self.requests, command_results=self.commands,
                    ground_sent_history_verified=self.histories, fault_reports=self.faults,
                    eps_owner_clears=self.clears, crc_retention_verified=self.retained,
                    driver_stack_free_min_bytes=self.stack_min,
                    limits="Selected simulated EPS input/real services/RTOS; not physical sensors, all fault owners or flight qualification")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--board-ip", default="192.168.1.5", type=ipaddress.IPv4Address)
    parser.add_argument("--reset", action="store_true", required=True)
    parser.add_argument("--send-mode-commands", action="store_true", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
        parser.error("Output must be a new file in an existing directory")
    evidence = LifecycleEvidence()
    sender = ModeSender(JsonHttp(args.url), board_ip=str(args.board_ip))
    stop, results = threading.Event(), SimpleQueue()
    worker, issued = None, 0
    started = time.monotonic()

    def send(number):
        try:
            sender.send(number, stop)
            results.put((number, True))
        except Exception:
            results.put((number, False))  # No arbitrary HTTP/credential details.

    with ExitStack() as cleanup:
        try: stream = cleanup.enter_context(args.output.open("x", encoding="utf-8"))
        except OSError: parser.error("Could not exclusively create output; serial port was not opened")
        try:
            write_snapshot(stream, evidence.snapshot(0, "RUNNING"))
            try:
                import serial
                with serial.Serial() as port:
                    port.port, port.baudrate, port.timeout = args.port, 115200, .1
                    port.dtr = port.rts = False
                    port.open()
                    port.reset_input_buffer()
                    port.rts = True
                    time.sleep(.1)
                    port.rts = False
                    started = time.monotonic()
                    framer = SerialLines()
                    while time.monotonic() - started < 150:
                        changed = False
                        for line in framer.feed(port.readline(1026)):
                            evidence.observe(line, time.monotonic() - started)
                            if line.startswith("TEST: LIFECYCLE"):
                                changed = True
                        while not results.empty():
                            number, verified = results.get()
                            if not verified or number != len(evidence.histories) + 1:
                                raise EvidenceError("Ground transmission/history verification failed; no retry")
                            evidence.histories.append(number)
                            changed = True
                        if issued < len(evidence.requests) and (worker is None or not worker.is_alive()):
                            issued += 1
                            worker = threading.Thread(target=send, args=(issued,), daemon=True)
                            worker.start()
                        if changed:
                            write_snapshot(stream, evidence.snapshot(time.monotonic() - started, "RUNNING"))
                            print(f"STATE: stages={len(evidence.states)} results={len(evidence.commands)} history={len(evidence.histories)}", flush=True)
                        if not framer.pending and evidence.complete(): break
                    else: raise EvidenceError("Lifecycle evidence deadline expired")
            finally:
                stop.set()
                if worker is not None:
                    worker.join(10)  # Must stop before the outer runner restores.
                    if worker.is_alive(): raise EvidenceError("Ground worker failed to stop before restore")
            write_snapshot(stream, evidence.snapshot(time.monotonic() - started, "PASS"))
            print("PASS: deployment denial, EPS fault/inhibition, owner clear with SAFE retained, separate ground mode/rearm, real payload/archive retention.", flush=True)
            return 0
        except (Exception, KeyboardInterrupt) as error:
            interrupted = isinstance(error, KeyboardInterrupt)
            reason = str(error) if isinstance(error, EvidenceError) else "Interrupted by user" if interrupted else "Reader I/O/setup/internal failure (raw details omitted)"
            try: write_snapshot(stream, evidence.snapshot(max(0, time.monotonic() - started), "INTERRUPTED" if interrupted else "FAIL", reason))
            except OSError: print("FAIL: final proof could not be saved; RUNNING is not success.", flush=True)
            print(("INTERRUPTED: " if interrupted else "FAIL: ") + reason, flush=True)
            return 130 if interrupted else 1


if __name__ == "__main__":
    raise SystemExit(main())
