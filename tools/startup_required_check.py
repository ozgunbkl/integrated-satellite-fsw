"""Explicit reset-only reader for required-startup failure images; never uploads.

Requires an armed boot, selected injected return/error, real abort and a second
boot reporting ESP_RST_PANIC, then bounded normal mission progress. Raw serial
and credentials are neither printed nor stored. Preserve RAM before --reset.
"""
import argparse
from contextlib import ExitStack
import math
from pathlib import Path
import re
import time

from resource_monitor_check import EvidenceError, SerialLines, write_snapshot
from watchdog_system_check import ARM, BOOT, CONFIG, CORE, FAULT, NOMINAL, RESOURCES, SOFTWARE, panic_diagnostic

ARCHIVE = "MAIN: RAM archive initialized before task creation."
HK_READY = "MAIN: HK and payload initialized before task creation (payload state=0)."
PROFILE = re.compile(r"TEST: STARTUP profile=(hk_init|task_early|task_late) injection=(armed|suppressed) reset_reason=(\d+)\.")
ABORT = re.compile(r"abort\(\) was called at PC 0x[0-9a-fA-F]+ on core [01]")
PROFILES = {"hk_init": None, "task_early": ("CMD_PROC", 5, 3), "task_late": ("SOLAR_HARV", 1, 11)}


class StartupEvidence:
    def __init__(self, profile, duration=45):
        if profile not in PROFILES or type(duration) is not int or not 30 <= duration <= 120:
            raise ValueError("Select a startup profile and integer duration from 30 to 120 seconds")
        self.profile, self.duration = profile, duration
        self.boots = 0
        self.config = None
        self.reset_reasons = []
        self.archive = self.profile_seen = self.hk_ready = self.resources = self.armed = self.core = False
        self.software_at = self.injected_at = self.failure_at = self.abort_at = None
        self.real_target_seen = False
        self.nominal_times = []
        self.last_now = None

    def observe(self, line, now):
        if (not isinstance(now, (int, float)) or isinstance(now, bool) or not math.isfinite(now) or now < 0 or
                (self.last_now is not None and now < self.last_now)):
            raise EvidenceError("Invalid/non-monotonic observation clock")
        self.last_now = now
        if len(line) > 1024:
            raise EvidenceError("Oversized serial line")
        if any(text in line for text in ("TEST ERROR:", "Brownout detector", "Task watchdog got triggered", "Interrupt wdt",
                "Telemetry Queue Full", "command queue full, dropped", "Resource diagnostics unavailable")):
            raise EvidenceError("Unexpected test/SDK/resource failure")
        fault = FAULT.search(line)
        if fault and int(fault[2]) >= 2:
            raise EvidenceError("Unexpected critical/fatal fault")
        if BOOT in line:
            if self.boots and (self.boots != 1 or self.abort_at is None):
                raise EvidenceError("Unexpected reboot without selected failure and abort")
            self.boots += 1
            self.config = None
            self.archive = self.profile_seen = self.hk_ready = self.resources = self.armed = self.core = False
            self.software_at = None
            self.nominal_times = []
            return
        if ABORT.fullmatch(line):
            if self.boots != 1 or self.failure_at is None or self.abort_at is not None or not 0 <= now - self.failure_at <= 10:
                raise EvidenceError("Unexpected/repeated/delayed abort")
            self.abort_at = now
            return
        diagnostic = panic_diagnostic(line)
        if diagnostic:
            if not line.startswith("Backtrace:") or self.boots != 1 or self.abort_at is None:
                raise EvidenceError("Unexpected panic/stack/assertion diagnostic")
            return  # No raw PC/backtrace text in the saved proof.
        if "abort()" in line or "Aborting." in line:
            raise EvidenceError("Malformed/unexpected abort diagnostic")
        if line.startswith("WATCHDOG: SDK recovery configured"):
            match = CONFIG.fullmatch(line)
            if not match or not self.boots or self.config is not None:
                raise EvidenceError("Malformed/out-of-order/repeated SDK configuration")
            self.config = tuple(map(int, match.groups()))
            if self.config[:3] != (15000, 3, 1) or not 0 <= self.config[3] <= 15 or (self.boots == 2 and self.config[3] != 4):
                raise EvidenceError("Wrong SDK recovery configuration/reset reason")
            self.reset_reasons.append(self.config[3])
        elif line == ARCHIVE:
            if self.config is None or self.archive:
                raise EvidenceError("Archive readiness out of order/repeated")
            self.archive = True
        elif line.startswith("TEST: STARTUP profile="):
            match = PROFILE.fullmatch(line)
            if not match or not self.archive or self.profile_seen or match[1] != self.profile or self.config is None:
                raise EvidenceError("Wrong/malformed/out-of-order/repeated startup profile")
            if match[2] != ("armed" if self.boots == 1 else "suppressed") or int(match[3]) != self.config[3] or (self.boots == 1 and self.config[3] == 4):
                raise EvidenceError("Invalid armed boot or panic suppression")
            self.profile_seen = True
        elif line.startswith("TEST: STARTUP injected"):
            target = PROFILES[self.profile]
            expected = ("TEST: STARTUP injected kind=hk_init return=false real_call=skipped." if target is None else
                        f"TEST: STARTUP injected kind=task_create task={target[0]} return=-1 real_call=skipped.")
            if self.boots != 1 or not self.profile_seen or self.injected_at is not None or line != expected or (target and not self.armed):
                raise EvidenceError("Wrong/repeated/out-of-order startup injection")
            self.injected_at = now
        elif "Failed to initialize" in line or "Required task creation failed:" in line:
            target = PROFILES[self.profile]
            expected = ("MAIN: Failed to initialize HK service" if target is None else
                        f"MAIN: Required task creation failed: {target[0]} (stack=4096, priority={target[1]}, required_created={target[2]}/12)")
            if (self.boots != 1 or self.injected_at is None or self.failure_at is not None or
                    not line.endswith(expected) or not 0 <= now - self.injected_at <= 10):
                raise EvidenceError("Wrong/unselected/repeated startup failure")
            self.failure_at = now
        elif line == HK_READY:
            if not self.profile_seen or self.hk_ready or (self.boots == 1 and self.profile == "hk_init"):
                raise EvidenceError("False/out-of-order HK readiness")
            self.hk_ready = True
        elif line == RESOURCES:
            if not self.hk_ready or self.resources:
                raise EvidenceError("Resource readiness out of order/repeated")
            self.resources = True
        elif line == ARM:
            if not self.resources or self.armed:
                raise EvidenceError("Watchdog arm out of order/repeated")
            self.armed = True
        elif line == SOFTWARE:
            if not self.armed or self.software_at is not None:
                raise EvidenceError("Supervisor start out of order/repeated")
            self.software_at = now
        elif line.startswith("TEST: STARTUP target="):
            target = PROFILES[self.profile]
            if (self.boots != 2 or not self.armed or target is None or self.real_target_seen or
                    line != f"TEST: STARTUP target={target[0]} real_create_return=1."):
                raise EvidenceError("Wrong/repeated/failed real recovery task creation")
            self.real_target_seen = True
        elif line.startswith(CORE):
            if (self.boots != 2 or not self.armed or self.core or
                    (PROFILES[self.profile] and not self.real_target_seen)):
                raise EvidenceError("False/out-of-order core readiness")
            self.core = True
        elif line == NOMINAL:
            if not self.armed or self.software_at is None or (self.boots == 1 and self.profile != "task_late"):
                raise EvidenceError("Mission before required task/supervisor readiness")
            if self.boots == 2:
                self.nominal_times.append(now)
        elif line.startswith("TEST:"):
            raise EvidenceError("Unexpected test profile/marker")
        elif any(text in line for text in ("Failed to arm", "Supervisor subscription failed", "Supervisor watchdog feed failed",
                                           "SDK task watchdog unavailable", "reconfiguration failed")):
            raise EvidenceError("Unexpected startup/watchdog failure")

    def complete(self, now):
        return (self.boots == 2 and self.profile_seen and self.core and self.software_at is not None and
                self.abort_at is not None and now - self.software_at >= self.duration and
                len(self.nominal_times) >= 3 and self.nominal_times[-1] - self.nominal_times[0] >= 9 and
                now - self.nominal_times[-1] <= 10)

    def snapshot(self, now, result, reason=None):
        return dict(result=result, reason=reason, profile=self.profile, observation_seconds=round(now, 3),
                    boots=self.boots, sdk_reset_reasons=self.reset_reasons.copy(), injected_at_s=self.injected_at,
                    failure_at_s=self.failure_at, abort_at_s=self.abort_at, recovery_core_ready=self.core,
                    recovery_real_target_created=self.real_target_seen, recovery_nominal_samples=len(self.nominal_times),
                    required_recovery_seconds=self.duration,
                    limits="Injected return codes, selected startup/panic/second-boot paths only; not physical heap exhaustion or persistent reboot-loop recovery")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--profile", choices=tuple(PROFILES), required=True)
    parser.add_argument("--duration", type=int, default=45, help="30..120 s after recovered supervisor starts")
    parser.add_argument("--reset", action="store_true", required=True, help="Explicit RAM-losing reset; preserve archive first")
    parser.add_argument("--output", type=Path, required=True, help="New JSON proof file in an existing directory")
    args = parser.parse_args(argv)
    try:
        evidence = StartupEvidence(args.profile, args.duration)
    except ValueError as error:
        parser.error(str(error))
    if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
        parser.error("Output must be a new file in an existing directory")
    with ExitStack() as cleanup:
        try:
            stream = cleanup.enter_context(args.output.open("x", encoding="utf-8"))
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
                port.rts = True
                time.sleep(.1)
                port.rts = False
                started = time.monotonic()
                framer = SerialLines()
                while True:
                    now = time.monotonic() - started
                    if now > args.duration + 150:
                        raise EvidenceError("Startup evidence deadline expired")
                    before = (evidence.boots, evidence.profile_seen, evidence.core, evidence.injected_at, evidence.failure_at, evidence.abort_at)
                    for line in framer.feed(port.readline(1026)):
                        evidence.observe(line, time.monotonic() - started)
                    now = time.monotonic() - started
                    after = (evidence.boots, evidence.profile_seen, evidence.core, evidence.injected_at, evidence.failure_at, evidence.abort_at)
                    if before != after:
                        write_snapshot(stream, evidence.snapshot(now, "RUNNING"))
                        print(f"STATE: boot={evidence.boots} selected_profile_seen={int(evidence.profile_seen)} "
                              f"injected={int(evidence.injected_at is not None)} abort={int(evidence.abort_at is not None)} "
                              f"recovery_core={int(evidence.core)}", flush=True)
                    if not framer.pending and evidence.complete(now):
                        write_snapshot(stream, evidence.snapshot(now, "PASS"))
                        print("PASS: selected startup failure -> abort -> ESP_RST_PANIC -> bounded second-boot mission progress.", flush=True)
                        return 0
        except (Exception, KeyboardInterrupt) as error:
            interrupted = isinstance(error, KeyboardInterrupt)
            reason = str(error) if isinstance(error, EvidenceError) else "Interrupted by user" if interrupted else "Reader I/O/setup/internal failure (raw details omitted)"
            try:
                write_snapshot(stream, evidence.snapshot(max(0, time.monotonic() - started), "INTERRUPTED" if interrupted else "FAIL", reason))
            except OSError:
                print("FAIL: final proof could not be saved; RUNNING is not success.", flush=True)
            print(("INTERRUPTED: " if interrupted else "FAIL: ") + reason, flush=True)
            return 130 if interrupted else 1


if __name__ == "__main__":
    raise SystemExit(main())
