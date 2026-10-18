"""Bounded serial-only resource observation; opens with DTR/RTS low, never resets.

Reports observed heap/stack/heartbeat intervals and sampled queue occupancy.
Not CPU utilization, WCET, an exact queue peak, or a long-duration qualification.
Arbitrary serial/event/network text is neither printed nor stored.
"""
import argparse
from contextlib import ExitStack
import json
import math
import os
from pathlib import Path
import re
import time

from watchdog_system_check import BOOT, FAULT, NOMINAL, panic_diagnostic

BUDGETS = {"WDT_MON": 4096, "EPS_MGMT": 4096, "CMD_PROC": 4096, "TC_RX": 4096,
           "EPS_MON": 2048, "FDIR": 4096, "DATA_LOG": 4096, "ADCS_MGMT": 4096, "RECOVERY": 6144,
           "TM_GEN": 4096, "SOLAR_HARV": 4096, "WIFI_MGR": 4096, "YLOG_TX": 3072, "RES_MON": 4096}
BEGIN = re.compile(r"DIAG: begin seq=(\d+) uptime_ms=(\d+) tick_ms=(\d+)\.")
HEAP = re.compile(r"DIAG: heap free=(\d+) min=(\d+) largest=(\d+)\.")
QUEUE = re.compile(r"DIAG: queue name=(TM|TC) used=(\d+) peak=(\d+) capacity=(\d+) sample_ms=(\d+)\.")
STACK = re.compile(r"DIAG: stack name=([A-Z_]+) free_min=(\d+) budget=(\d+)\.")
TIMING = re.compile(r"DIAG: timing id=(\d+) count=(\d+) min_ms=(\d+) max_ms=(\d+) age_ms=(\d+)\.")
OBSERVER = re.compile(r"DIAG: observer_ms=(\d+)\.")
END = re.compile(r"DIAG: end seq=(\d+)\.")
DEGRADED = re.compile(r"FDIR: Warning threshold handled for source \d+; DEGRADED decision committed\.")
MODE = re.compile(r"TM GEN: Waiting for MODE_NOMINAL\. Current mode: ([0-5])")
MODES = {0: "INITIAL_BOOT", 1: "SAFE", 2: "NOMINAL", 3: "DEGRADED", 4: "EMERGENCY", 5: "CRITICAL"}


class EvidenceError(RuntimeError):
    """Fixed diagnostic only; never include arbitrary serial or device text."""


class ResourceEvidence:
    def __init__(self, duration=600, *, require_mode=None):
        if type(duration) is not int or not 300 <= duration <= 7200:
            raise ValueError("Duration must be an integer from 300 to 7200 seconds")
        if require_mode is not None and require_mode not in MODES.values():
            raise ValueError("Unknown required mode")
        self.duration = duration
        self.require_mode = require_mode
        self.block = None
        self.reports = []
        self.last_now = None
        self.last_report_finished = None
        self.last_mode = self.last_mode_at = None
        self.mode_samples = {name: 0 for name in MODES.values()}
        self.mode_first_at = self.mode_max_gap = None
        self.nominal = self.archive_full = self.degraded = self.boot_markers = 0

    def _clock(self, now):
        if (not isinstance(now, (int, float)) or isinstance(now, bool) or not math.isfinite(now) or
                now < 0 or (self.last_now is not None and now < self.last_now)):
            raise EvidenceError("Invalid/non-monotonic observation clock")
        self.last_now = now

    def check_liveness(self, now):
        self._clock(now)
        if now - (self.last_report_finished if self.last_report_finished is not None else 0) > 45:
            raise EvidenceError("No complete resource report within 45 seconds")
        if self.require_mode is not None and now - (self.last_mode_at if self.last_mode_at is not None else 0) > 15:
            raise EvidenceError("Required mode evidence missing/stale for over 15 seconds")

    def observe(self, line, now):
        self._clock(now)
        if len(line) > 1024:
            raise EvidenceError("Oversized serial line")
        if panic_diagnostic(line) or any(s in line for s in ("abort()", "Brownout detector", "Task watchdog got triggered")):
            raise EvidenceError("SDK panic/reset/stack/watchdog diagnostic observed")
        if BOOT in line:
            self.boot_markers += 1
            if self.boot_markers > 1 or self.reports or self.block:
                raise EvidenceError("Unexpected reboot during observation")
        fault = FAULT.search(line)
        if fault and int(fault[2]) >= 2:
            raise EvidenceError("Critical/fatal FDIR fault during resource observation")
        if fault and int(fault[3], 16) == 0xAF01:
            self.archive_full += 1
        if line == NOMINAL:
            self.nominal += 1
            mode = "NOMINAL"
        elif match := MODE.fullmatch(line):
            mode = MODES[int(match[1])]
            if mode == "NOMINAL":
                raise EvidenceError("Contradictory mission mode diagnostic")
        else:
            mode = None
        if mode is not None:
            if self.require_mode is not None and mode != self.require_mode:
                raise EvidenceError("Observed mode differs from required " + self.require_mode)
            if self.last_mode_at is not None:
                self.mode_max_gap = max(self.mode_max_gap or 0, now - self.last_mode_at)
            else:
                self.mode_first_at = now
            self.last_mode, self.last_mode_at = mode, now
            self.mode_samples[mode] += 1
        if DEGRADED.fullmatch(line):
            self.degraded += 1
        if any(s in line for s in ("TEST:", "TEST ERROR:", "Resource diagnostics unavailable", "Telemetry Queue Full", "command queue full, dropped")):
            raise EvidenceError("Test image, missing diagnostics or mission queue drop")
        if not line.startswith("DIAG:"):
            return None
        if match := BEGIN.fullmatch(line):
            if self.block is not None:
                raise EvidenceError("Incomplete/interleaved diagnostic block")
            sequence, uptime, tick = map(int, match.groups())
            if not 0 < sequence <= 0xFFFFFFFF or not 0 <= uptime <= 0xFFFFFFFF or tick != 10:
                raise EvidenceError("Wrong diagnostic epoch/tick configuration")
            if self.reports:
                previous = self.reports[-1]
                delta = (uptime - previous["uptime_ms"]) & 0xFFFFFFFF
                if (sequence != previous["sequence"] + 1 or not 29000 <= delta <= 35000 or
                        not 25 <= now - previous["observed_at_s"] <= 40):
                    raise EvidenceError("Diagnostic report gap/reboot/buffered burst")
            self.block = dict(sequence=sequence, uptime_ms=uptime, observed_at_s=now, tick_ms=tick,
                              sampled_mode=(self.last_mode if self.last_mode_at is not None and now - self.last_mode_at <= 15 else None),
                              heap=None, queues={}, stacks={}, timings={}, observer_ms=None)
            return None
        if self.block is None:
            # A no-reset observer may attach midway through an existing block.
            # Ignore that partial first block, then require complete reports.
            if not self.reports:
                return None
            raise EvidenceError("Orphan diagnostic line; attach before a complete report")
        block = self.block
        if match := HEAP.fullmatch(line):
            free, minimum, largest = map(int, match.groups())
            if block["heap"] is not None or not 32768 <= free <= 4000000 or not 16384 <= minimum <= free or not 16384 <= largest <= free:
                raise EvidenceError("Duplicate/insufficient/invalid heap evidence")
            block["heap"] = dict(free=free, minimum=minimum, largest=largest)
        elif match := QUEUE.fullmatch(line):
            name = match[1]; used, peak, capacity, sample = map(int, match.groups()[1:])
            if name in block["queues"] or capacity != {"TM": 10, "TC": 5}[name] or sample != 100 or not 0 <= used <= peak <= capacity:
                raise EvidenceError("Duplicate/invalid queue evidence")
            block["queues"][name] = dict(used=used, sampled_peak=peak, capacity=capacity)
        elif match := STACK.fullmatch(line):
            name = match[1]; remaining, budget = map(int, match.groups()[1:])
            if name not in BUDGETS or name in block["stacks"] or budget != BUDGETS[name] or not 512 <= remaining <= budget:
                raise EvidenceError("Duplicate/unknown/low/invalid task stack evidence")
            block["stacks"][name] = remaining
        elif match := TIMING.fullmatch(line):
            task_id, count, minimum, maximum, age = map(int, match.groups())
            if task_id not in range(7) or task_id in block["timings"] or not 2 <= count <= 0xFFFFFFFF or not 0 <= minimum <= maximum <= 15000 or not 0 <= age <= 15000:
                raise EvidenceError("Duplicate/invalid/stalled heartbeat timing evidence")
            block["timings"][task_id] = dict(count=count, minimum_ms=minimum, maximum_ms=maximum, age_ms=age)
        elif match := OBSERVER.fullmatch(line):
            if block["observer_ms"] is not None or int(match[1]) > 1000:
                raise EvidenceError("Duplicate/slow diagnostic observer")
            block["observer_ms"] = int(match[1])
        elif match := END.fullmatch(line):
            if (int(match[1]) != block["sequence"] or block["heap"] is None or block["observer_ms"] is None or
                    set(block["stacks"]) != set(BUDGETS) or set(block["timings"]) != set(range(7)) or set(block["queues"]) != {"TM", "TC"}):
                raise EvidenceError("Incomplete/mismatched diagnostic report")
            if self.reports:
                previous = self.reports[-1]
                for name, value in block["stacks"].items():
                    if value > previous["stacks"][name]:
                        raise EvidenceError("Lifetime stack minimum increased")
                for name in block["queues"]:
                    if block["queues"][name]["sampled_peak"] < previous["queues"][name]["sampled_peak"]:
                        raise EvidenceError("Cumulative sampled queue peak decreased")
                for task_id, value in block["timings"].items():
                    before = previous["timings"][task_id]
                    if (value["count"] < before["count"] or
                            (value["count"] == before["count"] and value["count"] != 0xFFFFFFFF) or
                            value["minimum_ms"] > before["minimum_ms"] or value["maximum_ms"] < before["maximum_ms"]):
                        raise EvidenceError("Cumulative timing state reset/inconsistent/not progressing")
            self.reports.append(block); self.block = None
            self.last_report_finished = now
            return block
        else:
            raise EvidenceError("Malformed/unavailable resource diagnostic")
        return None

    def complete(self, elapsed):
        return (elapsed >= self.duration and self.block is None and len(self.reports) >= self.duration // 30 - 1 and
                self.reports[-1]["observed_at_s"] >= elapsed - 35 and
                self.reports[-1]["observed_at_s"] - self.reports[0]["observed_at_s"] >= self.duration - 35)

    def summary(self, elapsed):
        if not self.complete(elapsed):
            raise EvidenceError("Incomplete resource observation")
        self.check_liveness(elapsed)
        return self.snapshot(elapsed, "PASS")

    def snapshot(self, elapsed, result, reason=None):
        last = self.reports[-1] if self.reports else None
        return dict(result=result, reason=reason, requested_seconds=self.duration,
                    observation_seconds=round(elapsed, 1), reports=len(self.reports),
                    incomplete_block=self.block is not None, required_mode=self.require_mode,
                    mode_sample_counts=self.mode_samples.copy(), first_mode_at_s=self.mode_first_at,
                    last_mode_at_s=self.last_mode_at, mode_max_gap_s=self.mode_max_gap,
                    reports_without_recent_mode=sum(r["sampled_mode"] is None for r in self.reports),
                    heap_free_range=([min(r["heap"]["free"] for r in self.reports), max(r["heap"]["free"] for r in self.reports)] if last else None),
                    heap_free_first_to_last_delta=(last["heap"]["free"] - self.reports[0]["heap"]["free"] if last else None),
                    heap_minimum_lower_bound=min((r["heap"]["minimum"] for r in self.reports), default=None),
                    largest_free_block_min=min((r["heap"]["largest"] for r in self.reports), default=None),
                    task_stack_min_bytes=last["stacks"] if last else {}, heartbeat_timing=last["timings"] if last else {},
                    mission_queues=last["queues"] if last else {}, observer_max_ms=max((r["observer_ms"] for r in self.reports), default=None),
                    nominal_cycles=self.nominal, archive_full_warnings=self.archive_full, degraded_batches=self.degraded,
                    boot_markers_seen=self.boot_markers, samples=self.reports,
                    limits="13 persistent app tasks, 2 sampled queues, tick-quantized heartbeat; modes sampled from 5 s mission logs, not continuous mode/load proof; no CPU/WCET/all-load qualification")


class SerialLines:
    """Preserve timeout-split UART lines; never retain arbitrary raw logs."""
    def __init__(self):
        self.pending = b""

    def feed(self, raw):
        self.pending += raw
        while b"\n" in self.pending:
            raw_line, self.pending = self.pending.split(b"\n", 1)
            if len(raw_line) > 1024:
                raise EvidenceError("Oversized serial line")
            yield re.sub(r"\x1b\[[0-9;]*m", "", raw_line.decode("utf-8", errors="replace")).strip()
        if len(self.pending) > 1024:
            raise EvidenceError("Oversized serial line")


def write_snapshot(stream, report):
    if stream is not None:
        payload = json.dumps(report, indent=2, allow_nan=False) + "\n"
        stream.seek(0)
        stream.write(payload)
        stream.truncate()
        stream.flush()
        os.fsync(stream.fileno())


def observe_serial(port, evidence, started, *, stream=None, clock=time.monotonic):
    framer = SerialLines()
    while True:
        now = clock() - started
        evidence.check_liveness(now)
        if now > evidence.duration + 40:
            raise EvidenceError("Resource evidence deadline expired")
        raw = port.readline(1026)
        now = clock() - started
        evidence.check_liveness(now)
        for line in framer.feed(raw):
            block = evidence.observe(line, now)
            if block:
                write_snapshot(stream, evidence.snapshot(now, "RUNNING"))
                print(f"REPORT {block['sequence']}: uptime={block['uptime_ms']} ms free_heap={block['heap']['free']} "
                      f"stack_min={min(block['stacks'].values())} bytes mode={block['sampled_mode'] or 'UNKNOWN'} "
                      f"observer={block['observer_ms']} ms", flush=True)
        if not framer.pending and evidence.complete(now):
            return evidence.summary(now)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--duration", type=int, default=600, help="300..7200 seconds; 3600 for a one-hour observation")
    parser.add_argument("--require-mode", choices=tuple(MODES.values()), help="Fail on another sampled mode or missing mode logs over 15 s; does not command the board")
    parser.add_argument("--output", type=Path, help="New JSON file; RUNNING checkpoints and final PASS/FAIL/INTERRUPTED (never raw serial)")
    args = parser.parse_args(argv)
    try:
        evidence = ResourceEvidence(args.duration, require_mode=args.require_mode)
    except ValueError as error:
        parser.error(str(error))
    if args.output and (args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir()):
        parser.error("Output must be a new file in an existing directory")
    with ExitStack() as cleanup:
        try:
            stream = cleanup.enter_context(args.output.open("x", encoding="utf-8")) if args.output else None
        except OSError:
            parser.error("Could not exclusively create output; serial port was not opened")
        started = time.monotonic()
        try:
            write_snapshot(stream, evidence.snapshot(0, "RUNNING"))
            import serial
            with serial.Serial() as port:
                port.port, port.baudrate, port.timeout = args.port, 115200, .2
                port.dtr = port.rts = False
                port.open()
                port.reset_input_buffer()
                report = observe_serial(port, evidence, started, stream=stream, clock=time.monotonic)
        except (Exception, KeyboardInterrupt) as error:
            interrupted = isinstance(error, KeyboardInterrupt)
            reason = (str(error) if isinstance(error, EvidenceError) else
                      "Observation interrupted by user" if interrupted else "Observation I/O/setup or internal failure (raw details omitted)")
            report = evidence.snapshot(max(0, time.monotonic() - started), "INTERRUPTED" if interrupted else "FAIL", reason)
            try:
                write_snapshot(stream, report)
            except OSError:
                print("FAIL: could not save final report; prior RUNNING checkpoint is not a success.", flush=True)
            print(report["result"] + ": " + reason, flush=True)
            return 130 if interrupted else 1
        try:
            write_snapshot(stream, report)
        except OSError:
            print("FAIL: could not save final PASS report.", flush=True)
            return 1
        print("PASS: bounded resource observation; no reset/commands from this observer.", flush=True)
        print(json.dumps({k: v for k, v in report.items() if k != "samples"}, indent=2), flush=True)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
