"""Opt-in short persistent-recovery reader; no upload, NVS erase or fault clear.

Default --preflight only previews two commands (no serial/actual TC). An active
run explicitly resets RAM and issues at most four allowlisted TCs. Raw serial,
HTTP errors and credentials are never printed/saved. See recovery_target.md.
"""
import argparse
import binascii
from contextlib import ExitStack
import ipaddress
import math
from pathlib import Path
import re
import struct
import threading
import time
from queue import SimpleQueue
from urllib.error import HTTPError
from urllib.parse import quote

from resource_monitor_check import EvidenceError, SerialLines, write_snapshot
from startup_required_check import ABORT, ARCHIVE, HK_READY, PROFILE
from watchdog_recovery_check import ACK, NACK
from watchdog_system_check import ARM, BOOT, CONFIG, CORE, FAULT, NOMINAL, RESOURCES, SOFTWARE, panic_diagnostic
from yamcs_archive_transfer import JsonHttp, timestamp, unbase64

JOURNAL = re.compile(r"RECOVERY: boot=(\d+) failed_boots=(\d+) held=([01]) flags=(\d+); journal committed before core init\.")
STATUS = re.compile(r"RECOVERY: boot=(\d+) held=([01]) ready=([01]) storage_ok=([01]) stable=([01]) flags=(\d+) stack_free_bytes=(\d+)\.")
SAFE = "TM GEN: Waiting for MODE_NOMINAL. Current mode: 1"
DEPLOYED = "SYSTEM MODE: Deployment simulation completion recorded."
QUEUED = "RECOVERY: Ground rearm request queued; pending is not completion."
REJECTED = "RECOVERY: Ground rearm request rejected; pending is not completion."
COMPLETE = "RECOVERY: Ground rearm COMPLETE; boot gate released; system remains SAFE; history retained."
CRC = re.compile(r"TC PROC: \[CRC OK\] Packet Length: (18|22) bytes\. Handing to Router\.\.\.")
ROUTE = "CDHS: Routing packet to FDIR..."
KINDS = ("blocked", "stale", "approve", "nominal")
BASE_HISTORIES = (0, 2, 32, 34)  # Empty, nonfatal FAULT, BAD_RESET, or both. Never fatal/uncertain/overflow/boot-loop.
LOOP_PROFILE = re.compile(r"TEST: RECOVERY_LOOP injection=(armed|suppressed) reset_reason=(\d+) boot=(\d+) failed_boots=(\d+) target=3\.")
# Two existing boot demo commands are NOT the test's ground FDIR requests.
# Require their exact injector marker/CRC/route chain; never ignore arbitrary TC.
DEMO = []
for _apid, _command, _param, _route in ((0x040, 1, 2, "CDHS"), (0x020, 16, 1, "EPS")):
    _frame = struct.pack("!H", _apid) + bytes(12) + bytes((_command, _param))
    DEMO.append((f"INJECTOR: Sending APID 0x{_apid:03X}, CMD {_command}, CRC 0x{binascii.crc_hqx(_frame, 0xFFFF):04X}",
                 "CDHS: Routing packet to " + _route + "..."))


def command(kind, boot):
    if kind not in KINDS or type(boot) is not int or not 2 <= boot <= 0xFFFFFFFF:
        raise ValueError("Select a bounded recovery request with a valid current boot counter")
    if kind in ("blocked", "nominal"):
        return "/myproject/FDIR_SetMode", {"CmdParam": 2}, b"\x52\x02"
    value = boot - 1 if kind == "stale" else boot
    return "/myproject/FDIR_RearmBoot", {"BootCounter": value, "ConfirmKey": 165}, b"\x53" + struct.pack("!I", value) + b"\xA5"


def validate_binary(raw, body, postprocessed=False):
    if not isinstance(raw, bytes) or len(raw) != 6 + len(body):
        raise ValueError("Wrong recovery command length")
    packet, sequence, length = struct.unpack("!3H", raw[:6])
    if (packet != 0x1030 or sequence & 0xC000 != 0xC000 or
            length != (len(body) - 1 if postprocessed else 0) or raw[6:] != body):
        raise ValueError("Generated binary differs from the allowlisted recovery command")


class RecoverySender:
    def __init__(self, api, board_ip="192.168.1.5", *, clock=time.monotonic, sleep=time.sleep):
        self.api, self.clock, self.sleep = api, clock, sleep
        self.board_ip = str(ipaddress.IPv4Address(board_ip))
        self.attempted = []

    def _links(self, call):
        for name, kind in (("udp-in", "UdpTmDataLink"), ("udp-out", "UdpTcDataLink")):
            link = call("/api/links/myproject/" + name)
            if link.get("disabled") or link.get("status") != "OK" or link.get("type") != "org.yamcs.tctm." + kind:
                raise ValueError("Expected ready ground links")
            if name == "udp-out" and not re.search(re.escape("sending to " + self.board_ip + ":10025") + r"(?:$|[\s,;)])", link.get("detailedStatus", "")):
                raise ValueError("Ground command link targets another board")

    def _preview(self, call, kind, boot):
        name, args, body = command(kind, boot)
        path = "/api/processors/myproject/realtime/commands/" + quote(name, safe="")
        generated = call(path, {"args": args, "dryRun": True})
        if generated.get("commandName") != name:
            raise ValueError("Wrong generated command identity")
        validate_binary(unbase64(generated.get("unprocessedBinary")), body)
        return name, path, args, body

    def preflight(self):
        self._links(self.api)
        self._preview(self.api, "approve", 2)
        self._preview(self.api, "nominal", 2)

    def send(self, kind, boot, stop):
        # Consume the slot BEFORE any I/O: ambiguous delivery must never retry.
        command(kind, boot)
        if kind in self.attempted or (self.attempted and KINDS.index(kind) <= KINDS.index(self.attempted[-1])):
            raise ValueError("Repeated or out-of-order recovery request")
        self.attempted.append(kind)
        deadline = self.clock() + 8

        def call(path, body=None):
            remaining = deadline - self.clock()
            if remaining <= 0 or stop.is_set():
                raise RuntimeError("Recovery request cancelled/deadline expired")
            return self.api(path, body, timeout=min(4, remaining))

        self._links(call)
        name, path, args, body = self._preview(call, kind, boot)
        self._links(call)
        issued = call(path, {"args": args, "dryRun": False, "origin": "fsw-persistent-recovery-check",
                             "sequenceNumber": KINDS.index(kind) + 1})
        if issued.get("commandName") != name or not isinstance(issued.get("id"), str) or not issued["id"]:
            raise ValueError("Wrong issued command identity")
        timestamp(issued.get("generationTime"))
        validate_binary(unbase64(issued.get("unprocessedBinary")), body)
        while self.clock() < deadline:
            try:
                history = call("/api/archive/myproject/commands/" + quote(issued["id"], safe=""))
            except HTTPError as error:
                if error.code != 404:
                    raise
                self.sleep(.1)
                continue
            if history.get("id") != issued["id"] or history.get("commandName") != name:
                raise ValueError("Wrong command history identity")
            attrs = {}
            for row in history.get("attr", []):
                if row["name"] in attrs:
                    raise ValueError("Duplicate command history attribute")
                attrs[row["name"]] = row["value"]
            status = attrs.get("Acknowledge_Sent_Status", {}).get("stringValue")
            if status in ("NOK", "ERROR", "CANCELLED", "TIMEOUT"):
                raise RuntimeError("Ground rejected recovery transmission")
            if status == "OK" and "binary" in attrs:
                validate_binary(unbase64(attrs["binary"].get("binaryValue")), body, True)
                return  # Sent history is NOT board rearm completion.
            self.sleep(min(.1, max(0, deadline - self.clock())))
        raise RuntimeError("Recovery Sent history incomplete; no automatic retry")


class PersistentEvidence:
    def __init__(self, phase, previous_boot=None, *, expected_history=0, initial_boot=None, allow_fault_history=False,
                 allow_boot_loop_test=False, allow_boot_loop_history=False):
        if phase not in ("panic", "boot-loop", "restore", "fallback"):
            raise ValueError("Unknown persistent recovery phase")
        if previous_boot is not None and (type(previous_boot) is not int or not 1 <= previous_boot < 0xFFFFFFFF):
            raise ValueError("Previous boot counter is invalid/exhausted")
        if phase == "restore" and previous_boot is None:
            raise ValueError("Restore requires the successfully tested boot counter")
        histories = BASE_HISTORIES + tuple(value | 64 for value in BASE_HISTORIES)
        if type(expected_history) is not int or expected_history not in histories:
            raise ValueError("Only an exact bounded nonfatal/reset baseline history is supported")
        if type(allow_fault_history) is not bool or bool(expected_history & 2) != allow_fault_history:
            raise ValueError("Nonfatal FAULT history requires a separate exact-history approval")
        if type(allow_boot_loop_test) is not bool or allow_boot_loop_test != (phase == "boot-loop"):
            raise ValueError("BOOT_LOOP injection requires its separate explicit test approval")
        if (type(allow_boot_loop_history) is not bool or bool(expected_history & 64) != allow_boot_loop_history or
                (expected_history & 64 and phase not in ("restore", "fallback"))):
            raise ValueError("Sticky BOOT_LOOP history requires separate restore/fallback approval")
        maximum = 0xFFFFFFFA if phase == "boot-loop" else 0xFFFFFFFC
        if (phase == "boot-loop" and initial_boot is None) or (initial_boot is not None and
                (type(initial_boot) is not int or not 1 <= initial_boot <= maximum)):
            raise ValueError("Initial boot counter is invalid or lacks room for this package")
        self.phase, self.previous_boot = phase, previous_boot
        self.expected_history, self.initial_boot = expected_history, initial_boot
        self.fault_history_approved = bool(allow_fault_history)
        self.boot_loop_history_approved = allow_boot_loop_history
        self.injection_boots = 3 if phase == "boot-loop" else 1 if phase == "panic" else 0
        self.abort_count, self.loop_marker = 0, False
        self.rejected_journal = None
        self.boots = 0
        self.journals, self.reasons, self.requests, self.replies, self.histories = [], [], [], [], []
        self.last_now = self.core_at = self.ready_at = self.rearmed_at = None
        self.archive = self.profile = self.hk = self.resources = self.armed = self.software = self.core = self.deployed = False
        self.injected = self.failure = self.aborted = False
        self.pending = None
        self.crc = self.route = self.queued = self.released_seen = False
        self.stack_min = None
        self.safe_times, self.nominal_times = [], []
        self.demo_seen, self.demo_pending, self.demo_crc = [], None, False

    @property
    def boot(self):
        return self.journals[-1]["boot"] if self.journals else None

    @property
    def held(self):
        return bool(self.journals and self.journals[-1]["held"])

    def _clock(self, now):
        if (type(now) not in (int, float) or not math.isfinite(now) or now < 0 or
                (self.last_now is not None and now < self.last_now)):
            raise EvidenceError("Invalid/non-monotonic recovery clock")
        self.last_now = now

    def _reply(self, expected):
        if self.pending != expected or not self.crc or not self.route or expected in self.replies:
            raise EvidenceError("Unexpected/repeated/unattributed board command result")
        self.replies.append(expected)
        self.crc = self.route = False

    def next_request(self, now):
        self._clock(now)
        if not self.held or self.ready_at is None or len(self.replies) != len(self.requests) or self.histories != self.requests:
            return None
        if len(self.requests) == 4:
            return None
        if len(self.requests) == 3 and (self.rearmed_at is None or now - self.rearmed_at < 2 or
                not self.safe_times or self.safe_times[-1] < self.rearmed_at):
            return None  # Observe SAFE after approval before a separate mode request.
        return KINDS[len(self.requests)]

    def begin_request(self, kind, now):
        if kind != self.next_request(now):
            raise EvidenceError("Recovery request before required evidence")
        self.pending = kind
        self.requests.append(kind)

    def sent(self, kind):
        if len(self.histories) >= len(self.requests) or self.requests[len(self.histories)] != kind:
            raise EvidenceError("Out-of-order/duplicate recovery history")
        self.histories.append(kind)

    def observe(self, line, now):
        self._clock(now)
        if not isinstance(line, str) or len(line) > 1024:
            raise EvidenceError("Invalid/oversized recovery serial line")
        line = re.sub(r"\x1b\[[0-9;]*m", "", line).strip()
        if line == BOOT:
            if self.boots and not (self.injection_boots and self.boots <= self.injection_boots and self.aborted):
                raise EvidenceError("Unexpected reboot during recovery test")
            self.boots += 1
            self.injected = self.failure = self.aborted = self.loop_marker = False
            self.archive = self.profile = self.hk = self.resources = self.armed = self.software = self.core = self.deployed = False
            self.demo_seen, self.demo_pending, self.demo_crc = [], None, False
            return
        first_panic = bool(self.injection_boots and 1 <= self.boots <= self.injection_boots)
        if ABORT.fullmatch(line):
            if not first_panic or not self.failure or self.aborted:
                raise EvidenceError("Unexpected/repeated abort")
            self.aborted = True
            self.abort_count += 1
            return
        if panic_diagnostic(line):
            if not (first_panic and self.aborted and line.startswith("Backtrace:")):
                raise EvidenceError("Unexpected SDK panic/stack/assert diagnostic")
            return
        fault = FAULT.search(line)
        if (fault and int(fault[2]) >= 2) or any(value in line for value in (
                "TEST ERROR:", "abort()", "Aborting.", "Brownout detector", "Task watchdog got triggered",
                "Interrupt wdt", "Failed to arm", "Supervisor subscription failed", "watchdog feed failed",
                "SDK task watchdog unavailable", "reconfiguration failed", "Required task creation failed",
                "Telemetry Queue Full", "command queue full, dropped", "Resource diagnostics unavailable")):
            raise EvidenceError("Unexpected fault/startup/resource failure")
        if match := JOURNAL.fullmatch(line):
            boot, failed, held, flags = map(int, match.groups())
            observed = dict(boot=boot, failed_boots=failed, held=held, flags=flags)
            self.rejected_journal = observed  # Structured numeric evidence even on refusal, never raw serial.
            expected = self.expected_history | (32 if self.phase == "restore" or (self.injection_boots and self.boots > 1) else 0)
            if self.phase == "boot-loop" and self.boots == 4:
                expected |= 64
            allowed = (self.expected_history, self.expected_history | 32) if self.phase == "fallback" else (expected,)
            if (not self.boots or len(self.journals) != self.boots - 1 or len(self.reasons) != self.boots - 1 or
                    not 1 <= boot <= 0xFFFFFFFF or not 0 <= failed <= (3 if self.phase == "boot-loop" and self.boots == 4 else 2) or
                    flags not in tuple(value | 1 for value in allowed) or
                    bool(held) != bool(flags & ~1)):
                raise EvidenceError("Invalid/out-of-order/unsafe persistent journal")
            if self.journals:
                prior = self.journals[-1]
                if boot != prior["boot"] + 1 or failed != prior["failed_boots"] + 1 or not flags & 32 or not held:
                    raise EvidenceError("Panic boot lost its durable counter/history/inhibit")
            elif self.phase == "restore" and (boot != self.previous_boot + 1 or failed != 0 or not held):
                raise EvidenceError("Normal restore lost persistent history or per-boot gate")
            elif self.injection_boots and (failed != 0 or (self.initial_boot is not None and boot != self.initial_boot + 1)):
                raise EvidenceError("First selected boot lost the baseline counter or stable marker")
            elif self.phase == "fallback" and self.initial_boot is not None and boot <= self.initial_boot:
                raise EvidenceError("Fallback lost the baseline persistent boot counter")
            self.journals.append(observed)
            self.rejected_journal = None
        elif line.startswith("RECOVERY: boot="):
            match = STATUS.fullmatch(line)
            if not match or len(self.journals) != self.boots or not self.core or first_panic:
                raise EvidenceError("Malformed/out-of-order recovery status")
            boot, held, ready, storage, stable, flags, stack = map(int, match.groups())
            history = self.journals[-1]["flags"] & ~1
            if (boot != self.boot or not storage or flags & ~127 or flags & ~1 != history or
                    not 512 <= stack <= 6144 or (ready and (not stable or flags & 1)) or
                    (self.ready_at is not None and not ready) or
                    (self.held and not held and self.rearmed_at is None) or (not self.held and held)):
                raise EvidenceError("Recovery status lost health/history/storage/stack/gate")
            self.stack_min = stack if self.stack_min is None else min(stack, self.stack_min)
            if ready and self.ready_at is None:
                if not self.deployed or now - self.core_at < 30 or (self.held and
                        (len(self.safe_times) < 3 or self.safe_times[-1] - self.safe_times[0] < 9)):
                    raise EvidenceError("Premature recovery readiness without SAFE health window")
                self.ready_at = now
            if self.rearmed_at is not None and not held and ready:
                self.released_seen = True
        elif line.startswith("RECOVERY:"):
            if line == REJECTED:
                self._reply("stale")
            elif line == QUEUED:
                if self.pending != "approve" or self.queued or not self.crc or not self.route:
                    raise EvidenceError("Unexpected/duplicate queued approval")
                self.queued = True
            elif line == COMPLETE:
                if not self.queued or self.rearmed_at is not None:
                    raise EvidenceError("Approval completed without a queued request")
                self._reply("approve")
                self.rearmed_at = now
            else:
                raise EvidenceError("Recovery storage/monitor/command failed or unknown marker")
        elif line.startswith("WATCHDOG: SDK recovery configured"):
            match = CONFIG.fullmatch(line)
            if not match or len(self.journals) != self.boots or len(self.reasons) != self.boots - 1:
                raise EvidenceError("Malformed/out-of-order/repeated SDK configuration")
            timeout, mask, panic, reason = map(int, match.groups())
            if ((timeout, mask, panic) != (15000, 3, 1) or not 0 <= reason <= 15 or
                    (self.phase == "panic" and first_panic and reason == 4) or
                    (self.phase == "panic" and self.boots == 2 and reason != 4) or
                    (self.phase == "boot-loop" and (reason not in (1, 2, 3) if self.boots == 1 else reason != 4)) or
                    (self.phase in ("restore", "fallback") and reason not in (1, 2, 3))):
                raise EvidenceError("Wrong reset reason/SDK recovery configuration")
            self.reasons.append(reason)
        elif line == ARCHIVE:
            if len(self.reasons) != self.boots or self.archive:
                raise EvidenceError("Archive initialization out of order")
            self.archive = True
        elif line.startswith("TEST: RECOVERY_LOOP"):
            match = LOOP_PROFILE.fullmatch(line)
            if (self.phase != "boot-loop" or not match or not self.archive or self.loop_marker or
                    self.profile or match[1] != ("armed" if first_panic else "suppressed") or
                    tuple(map(int, match.groups()[1:])) != (self.reasons[-1], self.boot, self.boots - 1)):
                raise EvidenceError("Wrong/repeated/unbound boot-loop injection marker")
            self.loop_marker = True
        elif line.startswith("TEST: STARTUP profile="):
            match = PROFILE.fullmatch(line)
            if (not self.injection_boots or not match or not self.archive or self.profile or
                    (self.phase == "boot-loop" and not self.loop_marker) or
                    match[1] != "hk_init" or match[2] != ("armed" if first_panic else "suppressed") or
                    int(match[3]) != self.reasons[-1]):
                raise EvidenceError("Wrong/repeated/out-of-order startup test profile")
            self.profile = True
        elif line == "TEST: STARTUP injected kind=hk_init return=false real_call=skipped.":
            if not first_panic or not self.profile or self.injected:
                raise EvidenceError("Unexpected/repeated HK injection")
            self.injected = True
        elif "Failed to initialize" in line:
            if not first_panic or not self.injected or self.failure or not line.endswith("MAIN: Failed to initialize HK service"):
                raise EvidenceError("Unselected/repeated startup failure")
            self.failure = True
        elif line.startswith("TEST:"):
            raise EvidenceError("Unexpected test image or test marker")
        elif line == HK_READY:
            if not self.archive or self.hk or first_panic or (self.injection_boots and not self.profile):
                raise EvidenceError("False/out-of-order HK readiness")
            self.hk = True
        elif line == RESOURCES:
            if not self.hk or self.resources:
                raise EvidenceError("Required resources out of order")
            self.resources = True
        elif line == ARM:
            if not self.resources or self.armed:
                raise EvidenceError("Watchdog arm out of order")
            self.armed = True
        elif line == SOFTWARE:
            if not self.armed or self.software:
                raise EvidenceError("Supervisor readiness out of order")
            self.software = True
        elif line.startswith(CORE):
            if not self.software or self.core:
                raise EvidenceError("Core readiness out of order")
            self.core, self.core_at = True, now
        elif line == DEPLOYED:
            if not self.core or self.deployed:
                raise EvidenceError("Deployment completion out of order")
            self.deployed = True
        elif line == SAFE:
            if self.held:
                self.safe_times.append(now)
        elif line == NOMINAL or (line.startswith("SYSTEM MODE:") and line.endswith(" -> NOMINAL.")):
            if not self.core or not self.deployed or (self.held and
                    (self.rearmed_at is None or self.pending != "nominal" or "nominal" not in self.requests)):
                raise EvidenceError("Automatic/early NOMINAL bypassed the boot gate or separate command")
            if line == NOMINAL and (not self.held or "nominal" in self.replies):
                self.nominal_times.append(now)
        elif line.startswith("INJECTOR: Sending"):
            index = len(self.demo_seen)
            if (not self.core or self.ready_at is not None or self.pending is not None or self.demo_pending is not None or
                    index >= len(DEMO) or line != DEMO[index][0]):
                raise EvidenceError("Unexpected/repeated/out-of-order local demo command")
            self.demo_pending = DEMO[index][1]
        elif match := CRC.fullmatch(line):
            if self.demo_pending is not None:
                if self.demo_crc or int(match[1]) != 18:
                    raise EvidenceError("Wrong/repeated local demo CRC evidence")
                self.demo_crc = True
                return
            if (self.pending is None or self.pending in self.replies or self.crc or
                    int(match[1]) != (18 if self.pending in ("blocked", "nominal") else 22)):
                raise EvidenceError("Unexpected/repeated/wrong-length command ingress")
            self.crc = True
        elif line in (row[1] for row in DEMO):
            if line != self.demo_pending or not self.demo_crc:
                raise EvidenceError("Unattributed/wrong local demo route")
            self.demo_seen.append(line)
            self.demo_pending, self.demo_crc = None, False
        elif line == ROUTE:
            if not self.crc or self.route:
                raise EvidenceError("Unexpected/repeated command routing")
            self.route = True
        elif line == NACK:
            self._reply("blocked")
        elif line == ACK:
            self._reply("nominal")

    def complete(self, now):
        self._clock(now)
        return bool(self.core and self.deployed and self.ready_at is not None and self.stack_min is not None and
                    self.demo_seen == [row[1] for row in DEMO] and self.demo_pending is None and
                    (not self.injection_boots or (self.boots == self.injection_boots + 1 and
                     self.abort_count == self.injection_boots and self.profile)) and
                    (not self.held or (self.released_seen and self.requests == self.replies == self.histories == list(KINDS))) and
                    len(self.nominal_times) >= 3 and self.nominal_times[-1] - self.nominal_times[0] >= 9 and
                    now - self.nominal_times[-1] <= 10)

    def snapshot(self, now, result, reason=None):
        return dict(result=result, reason=reason, phase=self.phase, observation_seconds=round(now, 3),
                    boots=self.boots, journals=self.journals, sdk_reset_reasons=self.reasons,
                    selected_abort=bool(self.abort_count), selected_abort_count=self.abort_count,
                    ready_at_s=self.ready_at, rearmed_at_s=self.rearmed_at,
                    released_status_seen=self.released_seen, requests=self.requests, board_results=self.replies,
                    sent_history_verified=self.histories, nominal_samples=len(self.nominal_times),
                    recovery_stack_free_min_bytes=self.stack_min,
                    local_demo_routes_verified=self.demo_seen,
                    expected_baseline_history=self.expected_history, initial_boot=self.initial_boot,
                    fault_history_approved=self.fault_history_approved, rejected_journal=self.rejected_journal,
                    boot_loop_history_approved=self.boot_loop_history_approved,
                    limits="Selected " + ("three-PANIC BOOT_LOOP" if self.phase == "boot-loop" else "PANIC/normal-reset") +
                    ", journal and explicit approval only; no NVS erase/corruption, domain repair, power-cut or endurance proof")


class BaselineEvidence:
    """Passive status binding only, not an approval or a functional health proof."""
    def __init__(self, allow_boot_loop_history=False):
        self.status = None
        self.allow_boot_loop_history = allow_boot_loop_history

    def observe(self, line):
        if line == BOOT or panic_diagnostic(line):
            raise EvidenceError("Unexpected reboot/panic during passive baseline inspection")
        if line.startswith("RECOVERY: boot="):
            match = STATUS.fullmatch(line)
            if not match:
                raise EvidenceError("Malformed recovery status during passive inspection")
            boot, held, ready, storage, stable, flags, stack = map(int, match.groups())
            if not 1 <= boot <= 0xFFFFFFFC or not storage or flags & ~(99 if self.allow_boot_loop_history else 35) or not 512 <= stack <= 6144:
                raise EvidenceError("Unsupported baseline counter/history/storage/stack; no automatic repair")
            if not stable or flags & 1:
                return  # Observe a completed boot, never infer it from an unfinished journal.
            histories = BASE_HISTORIES + (tuple(value | 64 for value in BASE_HISTORIES) if self.allow_boot_loop_history else ())
            if flags not in histories or (flags == 0 and held):
                raise EvidenceError("Inconsistent baseline history/gate")
            self.status = dict(boot=boot, held=held, ready=ready, storage_ok=storage, stable=stable,
                               history_flags=flags, stack_free_bytes=stack)


def inspect_current(port_name, output, *, allow_boot_loop_history=False):
    evidence = BaselineEvidence(allow_boot_loop_history)
    report = dict(result="RUNNING", status=None, limits="Passive current status only; no reset, NVS writes, TC, or functional recovery claim")
    try:
        stream = output.open("x", encoding="utf-8")
    except OSError:
        raise ValueError("Could not exclusively create baseline proof; serial was not opened")
    with stream:
        try:
            write_snapshot(stream, report)
            import serial
            with serial.Serial() as port:
                port.port, port.baudrate, port.timeout = port_name, 115200, .1
                port.dtr = port.rts = False
                port.open()  # No reset toggle, purge, TC or HTTP call.
                started, framer = time.monotonic(), SerialLines()
                while time.monotonic() - started < 45 and evidence.status is None:
                    for line in framer.feed(port.readline(1026)):
                        evidence.observe(line)
                if evidence.status is None:
                    raise EvidenceError("No completed current recovery status within 45 seconds")
            report.update(result="PASS", status=evidence.status)
            write_snapshot(stream, report)
            print("PASS: passive baseline counter/history; no reset, NVS writes or TC.")
            return 0
        except (Exception, KeyboardInterrupt) as error:
            report.update(result="INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAIL",
                          reason=str(error) if isinstance(error, EvidenceError) else "Baseline read/output failed; raw details omitted")
            try:
                write_snapshot(stream, report)
            except OSError:
                print("FAIL: could not save final baseline proof; RUNNING is not success.")
            print(report["result"] + ": " + report["reason"])
            return 130 if report["result"] == "INTERRUPTED" else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--board-ip", type=ipaddress.IPv4Address, default=ipaddress.IPv4Address("192.168.1.5"))
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--inspect-current", action="store_true")
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--phase", choices=("panic", "boot-loop", "restore", "fallback"))
    parser.add_argument("--previous-boot", type=int)
    parser.add_argument("--expected-history", type=int, default=0)
    parser.add_argument("--initial-boot", type=int)
    parser.add_argument("--allow-fault-history", action="store_true")
    parser.add_argument("--allow-boot-loop-test", action="store_true")
    parser.add_argument("--allow-boot-loop-history", action="store_true")
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--allow-persistent-writes", action="store_true")
    parser.add_argument("--send-recovery-commands", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if args.inspect_current:
        if (any((args.preflight, args.phase, args.previous_boot is not None, args.expected_history,
                 args.initial_boot is not None, args.allow_fault_history, args.reset, args.allow_persistent_writes,
                 args.send_recovery_commands, args.allow_boot_loop_test)) or not args.output):
            parser.error("Passive baseline inspection cannot reset/write/send/approve or select an active phase")
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            parser.error("Output must be a new file in an existing directory")
        try:
            return inspect_current(args.port, args.output, allow_boot_loop_history=args.allow_boot_loop_history)
        except ValueError as error:
            parser.error(str(error))
    if args.preflight:
        if any((args.phase, args.previous_boot is not None, args.reset, args.allow_persistent_writes,
                args.send_recovery_commands, args.output, args.expected_history, args.initial_boot is not None,
                args.allow_fault_history, args.allow_boot_loop_test, args.allow_boot_loop_history)):
            parser.error("Preflight cannot reset/write/send or produce target evidence")
        try:
            RecoverySender(JsonHttp(args.url), str(args.board_ip)).preflight()
        except Exception:
            print("FAIL: expected ready links and recovery/mode MDB binary previews are unavailable.")
            return 1
        print("PASS: ready links and two dry-run binary previews; no serial, actual TC or file write.")
        return 0
    if not all((args.phase, args.reset, args.allow_persistent_writes, args.send_recovery_commands, args.output)):
        parser.error("Active run requires phase, reset, persistent-write/command approval and a new output file")
    try:
        evidence = PersistentEvidence(args.phase, args.previous_boot, expected_history=args.expected_history,
                                      initial_boot=args.initial_boot, allow_fault_history=args.allow_fault_history,
                                      allow_boot_loop_test=args.allow_boot_loop_test, allow_boot_loop_history=args.allow_boot_loop_history)
        sender = RecoverySender(JsonHttp(args.url), str(args.board_ip))
    except ValueError as error:
        parser.error(str(error))
    if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
        parser.error("Output must be a new file in an existing directory")
    stop, results = threading.Event(), SimpleQueue()
    worker, started = None, time.monotonic()

    def send(kind, boot):
        try:
            sender.send(kind, boot, stop)
            results.put((kind, True))
        except Exception:
            results.put((kind, False))

    with ExitStack() as cleanup:
        try:
            stream = cleanup.enter_context(args.output.open("x", encoding="utf-8"))
        except OSError:
            parser.error("Could not exclusively create proof; serial was not opened")
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
                    last_checkpoint, last_state = 0, None
                    while time.monotonic() - started < 240:
                        for line in framer.feed(port.readline(1026)):
                            evidence.observe(line, time.monotonic() - started)
                        while not results.empty():
                            kind, verified = results.get()
                            if not verified:
                                raise EvidenceError("Ground delivery/history failed or ambiguous; no retry")
                            evidence.sent(kind)
                        now = time.monotonic() - started
                        if not framer.pending and (worker is None or not worker.is_alive()):
                            kind = evidence.next_request(now)
                            if kind is not None:
                                evidence.begin_request(kind, now)
                                worker = threading.Thread(target=send, args=(kind, evidence.boot), daemon=True)
                                worker.start()
                        state = (evidence.boots, len(evidence.journals), evidence.ready_at, evidence.rearmed_at,
                                 len(evidence.replies), len(evidence.histories), len(evidence.nominal_times), evidence.released_seen)
                        if state != last_state or now - last_checkpoint >= 5:
                            write_snapshot(stream, evidence.snapshot(now, "RUNNING"))
                            last_state, last_checkpoint = state, now
                        if not framer.pending and evidence.complete(now):
                            break
                    else:
                        raise EvidenceError("Persistent recovery evidence deadline expired")
            finally:
                stop.set()
                if worker is not None:
                    worker.join(10)
                    if worker.is_alive():
                        raise EvidenceError("Ground worker did not stop before restore")
            write_snapshot(stream, evidence.snapshot(time.monotonic() - started, "PASS"))
            if args.phase == "fallback":
                print("PASS: fallback normal boot/health/mission progress only; NOT cross-upload history retention or selected-package success.")
            else:
                print("PASS: selected persistent boot/history, health, separate approval/SAFE/NOMINAL and bounded mission progress.")
            return 0
        except (Exception, KeyboardInterrupt) as error:
            interrupted = isinstance(error, KeyboardInterrupt)
            reason = str(error) if isinstance(error, EvidenceError) else "Interrupted by user" if interrupted else "Recovery reader I/O/setup/internal failure (raw details omitted)"
            try:
                write_snapshot(stream, evidence.snapshot(max(0, time.monotonic() - started), "INTERRUPTED" if interrupted else "FAIL", reason))
            except OSError:
                print("FAIL: final proof could not be saved; RUNNING is not success.")
            print(("INTERRUPTED: " if interrupted else "FAIL: ") + reason)
            return 130 if interrupted else 1


if __name__ == "__main__":
    raise SystemExit(main())
