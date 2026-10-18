"""Bounded real recovery evidence. Explicit reset + three allowlisted NOMINAL TCs.

No fault clear, archive command, SDK feed, raw UDP or server/link mutation.
The caller must preserve RAM first; the runner restores normal firmware even on failure.
"""
import argparse
import ipaddress
import re
import struct
import threading
import time
from queue import SimpleQueue
from urllib.parse import quote
from urllib.error import HTTPError

from watchdog_system_check import BOOT, SOFTWARE, CORE, RESOURCES, ARM, NOMINAL, CONFIG, STACK, FAULT, TASK_TIMEOUT, panic_diagnostic
from yamcs_archive_transfer import JsonHttp, timestamp, unbase64

PROFILE = "TEST: WDT profile=task_recovery target=0."
STATE = re.compile(r"TEST: WDT recovery stage=([0-6]) mode=([0-5]) active=([01]) revision=(\d+) count=([01]) uncertain=0 overflow=0 feed=ok\.")
MARK = re.compile(r"TEST: WDT recovery (drop|resume|ready_ground|operating)=([12])(?: target=0| mode=([23]))?\.")
CLEAR = "WATCHDOG: Task 0 stable; timeout resolved; mode unchanged."
ACK = "FDIR: System Mode manually set to NOMINAL by Ground."
NACK = "FDIR: Failed to set System Mode to NOMINAL by Ground."
CRC = "TC PROC: [CRC OK] Packet Length: 18 bytes. Handing to Router..."
ROUTE = "CDHS: Routing packet to FDIR..."


def validate_mode_binary(raw, postprocessed=False):
    if len(raw) != 8:
        raise ValueError("Mode command must be exactly eight bytes")
    packet, sequence, length = struct.unpack("!3H", raw[:6])
    if packet != 0x1030 or sequence & 0xC000 != 0xC000 or length != (1 if postprocessed else 0) or raw[6:] != b"\x52\x02":
        raise ValueError("Only the fixed APID 0x030 NOMINAL mode request is permitted")


class ModeSender:
    def __init__(self, api, *, board_ip="192.168.1.5", clock=time.monotonic, sleep=time.sleep):
        self.api, self.clock, self.sleep = api, clock, sleep
        self.board_ip = str(ipaddress.IPv4Address(board_ip))
        self.name = "/myproject/FDIR_SetMode"
        self.path = "/api/processors/myproject/realtime/commands/" + quote(self.name, safe="")

    def send(self, sequence, stop):
        if type(sequence) is not int or sequence not in (1, 2, 3):
            raise ValueError("Only three bounded mode requests are permitted")
        deadline = self.clock() + 8

        def call(path, body=None):
            remaining = deadline - self.clock()
            if remaining <= 0 or stop.is_set():
                raise RuntimeError("Mode request cancelled/deadline expired")
            return self.api(path, body, timeout=min(4, remaining))

        def links():
            for name, kind in (("udp-in", "UdpTmDataLink"), ("udp-out", "UdpTcDataLink")):
                link = call("/api/links/myproject/" + name)
                if link.get("disabled") or link.get("status") != "OK" or link.get("type") != "org.yamcs.tctm." + kind:
                    raise ValueError("Expected ready ground links")
                if name == "udp-out" and not re.search(re.escape("sending to " + self.board_ip + ":10025") + r"(?:$|[\s,;)])", link.get("detailedStatus", "")):
                    raise ValueError("Ground command link targets another board")

        links()
        dry = call(self.path, {"args": {"CmdParam": 2}, "dryRun": True})
        if dry.get("commandName") != self.name:
            raise ValueError("Wrong generated command identity")
        validate_mode_binary(unbase64(dry.get("unprocessedBinary")))
        links()  # Recheck immediately before the ONLY actual POST; never retry it.
        issued = call(self.path, {"args": {"CmdParam": 2}, "dryRun": False,
                                 "origin": "fsw-watchdog-recovery-check", "sequenceNumber": sequence})
        if issued.get("commandName") != self.name or not isinstance(issued.get("id"), str) or not issued["id"]:
            raise ValueError("Invalid issued command identity")
        timestamp(issued.get("generationTime"))
        validate_mode_binary(unbase64(issued.get("unprocessedBinary")))
        while self.clock() < deadline:
            try:
                history = call("/api/archive/myproject/commands/" + quote(issued["id"], safe=""))
            except HTTPError as exception:
                if exception.code != 404: raise
                self.sleep(.1)
                continue
            if history.get("id") != issued["id"] or history.get("commandName") != self.name:
                raise ValueError("Wrong command history identity")
            attrs = {}
            for row in history.get("attr", []):
                if row["name"] in attrs:
                    raise ValueError("Duplicate history attribute")
                attrs[row["name"]] = row["value"]
            status = attrs.get("Acknowledge_Sent_Status", {}).get("stringValue")
            if status in ("NOK", "ERROR", "CANCELLED", "TIMEOUT"):
                raise RuntimeError("Ground rejected mode transmission")
            if status == "OK" and "binary" in attrs:
                validate_mode_binary(unbase64(attrs["binary"].get("binaryValue")), True)
                return issued["id"]
            self.sleep(min(.1, max(0, deadline - self.clock())))
        raise RuntimeError("Mode transmission history incomplete; no automatic retry")


class RecoveryEvidence:
    def __init__(self):
        self.boots = self.stage = 0
        self.config = self.profile = self.resources = self.armed = self.core = False
        self.software = self.last_now = None
        self.drops, self.resumes, self.clears, self.ready, self.operating = [], [], [], [], []
        self.faults = self.timeouts = self.acks = self.nacks = self.crcs = 0
        self.revisions, self.final = [], []
        self.stack_min = None
        self.requests, self.verified = [], []
        self.nominal = []
        self.pending_crc = False

    def observe(self, line, now):
        line = re.sub(r"\x1b\[[0-9;]*m", "", line).strip()
        if self.last_now is not None and now < self.last_now:
            raise RuntimeError("Non-monotonic evidence clock")
        self.last_now = now
        if len(line) > 1024 or panic_diagnostic(line) or any(s in line for s in (
                "TEST ERROR:", "abort()", "Brownout detector", "Task watchdog got triggered", "Guru Meditation", "Backtrace:",
                "assert failed:", "Failed to initialize", "Required task creation failed", "Failed to arm",
                "Subscription failed", "watchdog feed failed", "Telemetry Queue Full", "command queue full, dropped")):
            raise RuntimeError("Unexpected panic/reset/startup/queue/test failure")
        if line == BOOT:
            self.boots += 1
            if self.boots != 1: raise RuntimeError("Unexpected reboot")
        elif match := CONFIG.fullmatch(line):
            if self.boots != 1 or self.config or tuple(map(int, match.groups()[:3])) != (15000, 3, 1):
                raise RuntimeError("Wrong/duplicate SDK recovery policy")
            self.config = True
        elif line == PROFILE:
            if not self.config or self.profile: raise RuntimeError("Wrong/duplicate recovery profile")
            self.profile = True
        elif line.startswith("TEST: WDT profile="):
            raise RuntimeError("Wrong test image")
        elif line == RESOURCES:
            self.resources = True
        elif line == ARM:
            if not self.resources or self.armed: raise RuntimeError("Wrong arm/resource order")
            self.armed = True
        elif line.startswith(CORE): self.core = True
        elif line == SOFTWARE:
            if not self.armed or self.software is not None: raise RuntimeError("Wrong monitor startup")
            self.software = now
        elif line == NOMINAL: self.nominal.append(now)
        elif match := FAULT.search(line):
            source, severity, code = int(match[1]), int(match[2]), int(match[3], 16)
            if severity >= 2:
                if (source, severity, code) != (4, 2, 0xC200) or self.stage not in (0, 3) or self.faults >= 2 or len(self.drops) != self.faults + 1:
                    raise RuntimeError("Wrong/extra critical cause or episode")
                if now - self.drops[-1] < 10: raise RuntimeError("Premature task timeout")
                # Immediate loss can expire before TM_GEN's first 5 s NOMINAL
                # log: WDT_MON has higher priority. The separate normal baseline
                # establishes initial mission progress; require full startup here.
                if not self.armed or not self.core or self.software is None:
                    raise RuntimeError("Fault before complete startup")
                self.faults += 1
        elif match := TASK_TIMEOUT.search(line):
            if int(match[1]) != 0 or self.timeouts >= self.faults: raise RuntimeError("Wrong/duplicate task timeout")
            self.timeouts += 1
        elif line == CLEAR:
            episode = len(self.clears)
            if len(self.resumes) != episode + 1 or now - self.resumes[episode] < 29 or (episode == 0 and self.nacks != 1):
                raise RuntimeError("Premature/unproven timeout clear")
            self.clears.append(now)
        elif line == NACK:
            if self.requests != ["blocked"] or self.nacks or self.clears:
                raise RuntimeError("Unexpected mode rejection")
            self.nacks = 1
        elif line == ACK:
            if len(self.ready) != self.acks + 1 or len(self.requests) != self.acks + 2 or self.acks >= 2:
                raise RuntimeError("Automatic/premature/duplicate NOMINAL acceptance")
            self.acks += 1
        elif line == CRC: self.pending_crc = True
        elif line == ROUTE:
            if not self.pending_crc: raise RuntimeError("FDIR route without verified internal CRC")
            self.pending_crc = False
            self.crcs += 1
        elif line.startswith("CDHS: Routing packet to "): self.pending_crc = False
        elif match := STACK.fullmatch(line):
            value = int(match[1])
            if value < 512: raise RuntimeError("Insufficient watchdog stack headroom")
            self.stack_min = value if self.stack_min is None else min(self.stack_min, value)
        elif match := MARK.fullmatch(line):
            kind, episode = match[1], int(match[2])
            rows = {"drop": self.drops, "resume": self.resumes, "ready_ground": self.ready, "operating": self.operating}[kind]
            if len(rows) != episode - 1: raise RuntimeError("Wrong/duplicate recovery marker")
            if kind == "drop":
                if episode == 1 and self.stage != 0: raise RuntimeError("Drop outside expected phase")
                if episode == 2 and (self.stage not in (2, 3) or len(self.ready) != 1 or len(self.requests) != 2):
                    raise RuntimeError("Second drop without separate ground-request phase")
            elif kind == "resume":
                if self.faults != episode or self.timeouts != episode: raise RuntimeError("Resume before real timeout")
                if episode == 1: self.requests.append("blocked")
            elif kind == "ready_ground":
                if len(self.clears) != episode: raise RuntimeError("Ready without owner clear")
                self.requests.append("first" if episode == 1 else "second")
            elif len(self.ready) != episode or len(self.requests) != episode + 1 or match[3] not in ("2", "3"):
                # MODE commits before the command handler logs ACK. On another
                # core the hook can observe it first; all ACK/Sent proofs are
                # still mandatory at completion, not assumed from log order.
                raise RuntimeError("Operating mode outside separate ground-request phase")
            rows.append(now)
        elif match := STATE.fullmatch(line):
            stage, mode, active, revision, count = map(int, match.groups())
            if not all((self.boots == 1, self.config, self.profile, self.armed, self.software is not None)):
                raise RuntimeError("Recovery state before complete startup")
            if stage < self.stage or stage > self.stage + 1 or count != active or bool(revision) != bool(active):
                raise RuntimeError("Wrong state transition/ledger identity")
            if stage in (1, 4) and (mode != 1 or not active): raise RuntimeError("Fault not retained in SAFE")
            if stage in (2, 5) and (mode != 1 or active): raise RuntimeError("Clear changed SAFE or left cause")
            if stage in (3, 6) and (mode not in (2, 3) or active): raise RuntimeError("Wrong operating state")
            if active:
                episode = 0 if stage == 1 else 1
                if len(self.revisions) == episode:
                    if episode and revision <= self.revisions[0]: raise RuntimeError("Second timeout reused old revision")
                    self.revisions.append(revision)
                elif len(self.revisions) != episode + 1 or self.revisions[episode] != revision:
                    raise RuntimeError("Timeout record changed unexpectedly")
            self.stage = stage
            if stage == 6: self.final.append(now)
        elif line.startswith("TEST: WDT recovery"):
            raise RuntimeError("Malformed recovery evidence")
        return line

    def complete(self):
        mission = [when for when in self.nominal if len(self.operating) == 2 and when >= self.operating[-1]]
        return (self.boots == 1 and self.core and self.stage == 6 and self.faults == self.timeouts == 2 and
                len(self.clears) == len(self.ready) == len(self.operating) == len(self.revisions) == 2 and
                self.acks == 2 and self.nacks == 1 and self.crcs == 3 and self.stack_min is not None and
                self.verified == ["blocked", "first", "second"] and len(self.final) >= 4 and self.final[-1] - self.final[0] >= 14 and
                len(mission) >= 3 and mission[-1] - mission[0] >= 9)


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--board-ip", default="192.168.1.5", type=ipaddress.IPv4Address)
    parser.add_argument("--reset", action="store_true", required=True)
    parser.add_argument("--send-mode-commands", action="store_true", required=True)
    args = parser.parse_args()
    evidence = RecoveryEvidence()
    sender = ModeSender(JsonHttp(args.url), board_ip=str(args.board_ip))
    stop, results = threading.Event(), SimpleQueue()
    worker, issued = None, 0

    def send(label, sequence):
        try: results.put((label, sender.send(sequence, stop), None))
        except Exception as exception: results.put((label, None, exception))

    try:
        with serial.Serial() as port:
            port.port, port.baudrate, port.timeout = args.port, 115200, .1
            port.dtr = port.rts = False
            port.open(); port.reset_input_buffer()
            port.rts = True; time.sleep(.1); port.rts = False
            started = time.monotonic()
            while time.monotonic() - started < 260:
                raw = port.readline(1026).decode("utf-8", errors="replace")
                now = time.monotonic()
                line = evidence.observe(raw, now)
                if line.startswith((BOOT, "WATCHDOG:", "TEST: WDT", "TEST ERROR:", RESOURCES, CORE, NOMINAL, "FDIR: System Mode", "FDIR: Failed to set")) or FAULT.search(line) or line == CRC:
                    print(line, flush=True)
                while not results.empty():
                    label, identity, exception = results.get()
                    if exception is not None: raise RuntimeError("Mode-command verification failed") from exception
                    evidence.verified.append(label)
                    print("GROUND: verified NOMINAL transmission " + label + ".", flush=True)
                if (worker is None or not worker.is_alive()) and issued < len(evidence.requests):
                    label = evidence.requests[issued]; issued += 1
                    worker = threading.Thread(target=send, args=(label, issued), daemon=True)
                    worker.start()
                if evidence.complete():
                    delays = [round(end - start, 3) for start, end in zip(evidence.resumes, evidence.clears)]
                    print(f"PASS: two real C200 episodes/revisions={evidence.revisions}, stable-clear delays={delays}s, SAFE retained, one rejected/two accepted ground requests, no reboot; WDT stack minimum={evidence.stack_min} bytes ({now-started:.1f}s).", flush=True)
                    return
            raise RuntimeError("Recovery evidence incomplete within 260 s")
    finally:
        stop.set()
        if worker is not None:
            worker.join(10)  # Finish/cancel BEFORE the runner restores normal firmware.
            if worker.is_alive(): raise RuntimeError("Ground worker failed to stop before restore")


if __name__ == "__main__":
    try: main()
    except (OSError, RuntimeError, ValueError, KeyError) as exception:
        raise SystemExit("FAIL: " + str(exception)) from exception
