"""Bounded serial evidence for normal, heartbeat-loss, TWDT and IWDT test images."""
import argparse
import re
import time

BOOT = "--- Integrated Satellite FSW Initialization Started ---"
SOFTWARE = "WATCHDOG: Software Watchdog initialized."
CORE = "MAIN: Core tasks created."
RESOURCES = "MAIN: Required queues and EPS mutex ready before Wi-Fi/log initialization."
ARM = "WATCHDOG: Supervision armed before task creation (7 critical tasks)."
NOMINAL = "TM GEN: -- Running NOMINAL Mission Cycle --"
LOSS_PROFILE = "TEST: WDT profile=task_loss target=0; no task suspension or SDK feed change."
DROP = "TEST: WDT heartbeat loss injected target=0."
STALL = "TEST: WDT supervisor stall injected after 6 completed checks."
CONFIG = re.compile(r"^WATCHDOG: SDK recovery configured timeout=(\d+) idle_mask=(\d+) panic=(\d+) reset_reason=(\d+)\.$")
PROFILE = re.compile(r"^TEST: WDT profile=supervisor_stall injection=(armed|suppressed) reset_reason=(\d+)\.$")
STARVE_PROFILE = re.compile(r"^TEST: WDT profile=(idle_starvation|interrupt_mask) injection=(armed|suppressed) reset_reason=(\d+) core=([01])\.$")
STARVE_CHECK = re.compile(r"^TEST: WDT starvation supervisor check=(\d+) feed=ok\.$")
IDLE_INJECT = "TEST: WDT idle starvation injected core=0 interrupts=enabled."
INTERRUPT_INJECT = "TEST: WDT interrupt masking injected core=0 timeout_ms=300."
STARVE_INJECT = re.compile(r"^TEST: WDT (idle starvation|interrupt masking) injected core=([01]) (interrupts=enabled|timeout_ms=300)\.$")
GLOBAL_PROFILE = re.compile(r"^TEST: WDT profile=global_starvation injection=(armed|suppressed) reset_reason=(\d+) cores=3\.$")
GLOBAL_INJECT = "TEST: WDT global starvation injected cores=3 ready=3 priority=24 interrupts=enabled."
RESET_MODES = ("supervisor_stall", "idle_starvation", "interrupt_mask", "global_starvation")
STATE = re.compile(r"^TEST: WDT task_loss mode=(\d+) active=(\d+) reports=(\d+) uncertain=(\d+) overflow=(\d+) feed=ok\.$")
STACK = re.compile(r"^TEST: WDT stack_free_min_bytes=(\d+)\.$")
FAULT = re.compile(r"FDIR: Fault from Source (\d+) \(Severity: (\d+)\) Code: 0x([0-9A-Fa-f]+)")
TASK_TIMEOUT = re.compile(r"WATCHDOG: !!! CRITICAL FAILURE: Task (\d+) timed out !!!")


def panic_diagnostic(line):
    """Only known panic classes/task names and numeric backtrace; never raw logs."""
    if match := re.search(r"Interrupt wdt timeout on CPU([01])\b", line):
        return "SDK: interrupt watchdog timeout on CPU" + match[1]
    if "stack overflow" in line.lower():
        task = re.search(r"\b(WDT_MON|TM_GEN|EPS_MGMT|FDIR|main)\b", line)
        return "SDK: stack overflow detected" + (" in " + task[1] if task else "")
    if "Stack canary watchpoint triggered" in line:
        task = re.search(r"\b(WDT_MON|TM_GEN|EPS_MGMT|FDIR|main)\b", line)
        return "SDK: stack canary triggered" + (" in " + task[1] if task else "")
    if "Guru Meditation" in line:
        for kind in ("LoadProhibited", "StoreProhibited", "IllegalInstruction", "Unhandled debug exception"):
            if kind in line:
                return "SDK: panic class " + kind
        return "SDK: unexpected panic class"
    if "Backtrace:" in line:
        pairs = re.findall(r"0x[0-9a-fA-F]+:0x[0-9a-fA-F]+", line)
        return "SDK: numeric backtrace " + " ".join(pairs)
    if "assert failed:" in line:
        return "SDK: assertion failed"
    return None


class WatchdogEvidence:
    def __init__(self, mode, duration, *, fault_core=0):
        if mode not in ("normal", "task_loss", *RESET_MODES):
            raise ValueError("Unknown watchdog mode")
        minimum = 60 if mode == "normal" else 45 if mode == "task_loss" else 30
        if type(duration) is not int or not minimum <= duration <= 600:
            raise ValueError("Observation is too short")
        if type(fault_core) is not int or fault_core not in (0, 1):
            raise ValueError("Fault core must be 0 or 1")
        if fault_core != 0 and mode not in ("idle_starvation", "interrupt_mask"):
            raise ValueError("Core selection is only valid for starvation modes")
        self.fault_core = fault_core
        self.mode, self.duration = mode, duration
        self.boots = 0
        self.config = None
        self.resources = self.armed = self.core = self.profile = False
        self.software_time = None
        self.nominal_times = []
        self.drop_time = self.task_timeout_time = self.stall_time = None
        self.fault_seen = False
        self.safe_times = []
        self.sdk_trigger_time = None
        self.sdk_entries = self.failed_supervisor = False
        self.failed_idle = self.interrupt_seen = False
        self.failed_global = set()
        self.global_running = set()
        self.global_running_section = False
        self.supervisor_checks = 0
        self.post_injection_feeds = []
        self.panic_seen = False
        self.stack_min = None
        self.stack_seen = False
        self.last_now = None

    def expected_panic(self):
        if self.mode == "global_starvation":
            # SDK has_reset flags describe a shared round, not per-task ages.
            # Require an unserved supervisor AND both workers in the SDK's
            # separate current-core snapshot; idle flags can already be true.
            return "WDT_MON" in self.failed_global and self.global_running == {0, 1} and self.sdk_trigger_time is not None
        if self.mode == "interrupt_mask": return self.interrupt_seen
        if self.mode == "idle_starvation": return self.failed_idle and self.sdk_trigger_time is not None
        return self.mode == "supervisor_stall" and self.failed_supervisor and self.sdk_trigger_time is not None

    def observe(self, line, now):
        line = re.sub(r"\x1b\[[0-9;]*m", "", line).strip()
        if self.last_now is not None and now < self.last_now:
            raise RuntimeError("Non-monotonic observation clock")
        self.last_now = now
        if len(line) > 1024:
            raise RuntimeError("Oversized serial evidence line")
        if "stack overflow" in line.lower() or "Stack canary watchpoint triggered" in line or "assert failed:" in line:
            raise RuntimeError("Unexpected stack/assertion failure")
        if "TEST ERROR:" in line or any(x in line for x in (
                "Required task creation failed", "Failed to initialize", "Failed to arm",
                "Supervisor subscription failed", "Supervisor watchdog feed failed",
                "SDK task watchdog unavailable", "reconfiguration failed", "Telemetry Queue Full")):
            raise RuntimeError("Startup/test/SDK API failure")
        if "Task watchdog got triggered" in line:
            if self.mode not in ("supervisor_stall", "idle_starvation", "global_starvation") or self.stall_time is None or self.boots != 1:
                raise RuntimeError("Unexpected SDK task watchdog timeout")
            if self.sdk_trigger_time is not None or now - self.stall_time < 10:
                raise RuntimeError("Repeated or premature SDK timeout")
            self.sdk_trigger_time = now
            self.sdk_entries = True
        elif self.sdk_entries and "Tasks currently running:" in line:
            self.sdk_entries = False
            self.global_running_section = self.mode == "global_starvation" and self.boots == 1
        elif self.sdk_entries and self.mode == "global_starvation" and re.search(r"-\s+\S+\s+\(CPU", line):
            subscriber = re.search(r"-\s+(WDT_MON|IDLE0|IDLE1)\s+\(CPU\s+(0/1|[01])\)", line)
            if (not subscriber or subscriber[1] in self.failed_global or
                    (subscriber[1].startswith("IDLE") and subscriber[1][-1] != subscriber[2])):
                raise RuntimeError("Wrong/extra/repeated global watchdog subscriber")
            self.failed_global.add(subscriber[1])
        elif self.global_running_section and (running := re.search(r"CPU\s+([01]):\s+(\S+)", line)):
            core = int(running[1])
            if running[2] != f"WDT_G{core}" or core in self.global_running:
                raise RuntimeError("Wrong/repeated task in global core snapshot")
            self.global_running.add(core)
        elif self.sdk_entries and self.mode == "supervisor_stall" and re.search(r"-\s+WDT_MON\s+\(CPU", line):
            self.failed_supervisor = True
        elif self.sdk_entries and self.mode == "idle_starvation" and re.search(
                rf"-\s+IDLE{self.fault_core}\s+\(CPU\s+{self.fault_core}\)", line):
            self.failed_idle = True
        elif self.sdk_entries and re.search(r"-\s+\S+\s+\(CPU", line):
            raise RuntimeError("Another SDK subscriber also stopped")
        interrupt = re.search(r"Interrupt wdt timeout on CPU(\d+)\b", line)
        if interrupt:
            if (self.mode != "interrupt_mask" or self.boots != 1 or self.stall_time is None or int(interrupt[1]) != self.fault_core or
                    self.interrupt_seen or not .05 <= now - self.stall_time <= 5):
                raise RuntimeError("Wrong/repeated/premature interrupt watchdog panic")
            self.interrupt_seen = True
            self.sdk_trigger_time = now
        if self.mode == "interrupt_mask" and "Guru Meditation" in line and not interrupt:
            raise RuntimeError("Unexpected non-IWDT panic class")
        if any(x in line for x in ("Guru Meditation", "Backtrace:", "abort()", "Aborting.")):
            if self.boots != 1 or not self.expected_panic():
                raise RuntimeError("Unexpected panic/abort/backtrace")
            self.panic_seen = True
        if BOOT in line:
            if self.boots and (self.mode not in RESET_MODES or self.boots != 1 or
                               not self.panic_seen or not self.expected_panic()):
                raise RuntimeError("Unexpected firmware reboot")
            self.boots += 1
            self.config = None
            self.resources = self.armed = self.core = self.profile = False
            self.software_time = None
            self.nominal_times = []
            self.stack_seen = False
            self.supervisor_checks = 0
            self.global_running_section = False
            return
        match = CONFIG.fullmatch(line)
        if line.startswith("WATCHDOG: SDK recovery configured"):
            if not match or self.boots == 0 or self.config is not None:
                raise RuntimeError("Missing/malformed/repeated SDK configuration")
            self.config = tuple(map(int, match.groups()))
            if self.config[:3] != (15000, 3, 1):
                raise RuntimeError("Incorrect shared timeout, idle mask or panic policy")
            expected_reason = 5 if self.mode == "interrupt_mask" else 6
            if self.boots == 2 and self.config[3] != expected_reason:
                raise RuntimeError("Recovery boot has the wrong actual watchdog reset reason")
        elif line == RESOURCES:
            if self.config is None or self.resources:
                raise RuntimeError("Resource readiness out of order/repeated")
            self.resources = True
        elif line == ARM:
            if not self.resources or self.armed:
                raise RuntimeError("Watchdog arm out of order/repeated")
            self.armed = True
        elif line == SOFTWARE:
            if not self.armed or self.software_time is not None:
                raise RuntimeError("Supervisor start out of order/repeated")
            self.software_time = now
        elif line.startswith(CORE):
            if not self.armed or self.core:
                raise RuntimeError("Core readiness out of order/repeated")
            self.core = True
        elif line == NOMINAL:
            if not self.core or self.software_time is None:
                raise RuntimeError("Mission cycle before core/supervisor readiness")
            self.nominal_times.append(now)
        elif line == LOSS_PROFILE:
            if self.mode != "task_loss" or self.config is None or self.profile:
                raise RuntimeError("Wrong or repeated task-loss profile")
            self.profile = True
        elif line.startswith("TEST: WDT profile=supervisor_stall"):
            match = PROFILE.fullmatch(line)
            if self.mode != "supervisor_stall" or not match or self.config is None or self.profile:
                raise RuntimeError("Wrong/malformed/repeated supervisor profile")
            state, reason = match.groups()
            expected = "armed" if self.boots == 1 else "suppressed"
            if state != expected or int(reason) != self.config[3]:
                raise RuntimeError("Injection/reboot suppression is incorrect")
            self.profile = True
        elif line.startswith(("TEST: WDT profile=idle_starvation", "TEST: WDT profile=interrupt_mask")):
            match = STARVE_PROFILE.fullmatch(line)
            if not match or self.config is None or self.profile:
                raise RuntimeError("Malformed/repeated starvation profile")
            mode, state, reason, core = match.groups()
            if (mode != self.mode or state != ("armed" if self.boots == 1 else "suppressed") or
                    int(reason) != self.config[3] or int(core) != self.fault_core):
                raise RuntimeError("Wrong starvation mode/core/reboot suppression")
            self.profile = True
        elif line.startswith("TEST: WDT profile=global_starvation"):
            match = GLOBAL_PROFILE.fullmatch(line)
            if self.mode != "global_starvation" or not match or self.config is None or self.profile:
                raise RuntimeError("Wrong/malformed/repeated global profile")
            if match[1] != ("armed" if self.boots == 1 else "suppressed") or int(match[2]) != self.config[3]:
                raise RuntimeError("Wrong global reboot suppression/reset reason")
            self.profile = True
        elif line.startswith("TEST: WDT starvation supervisor check="):
            match = STARVE_CHECK.fullmatch(line)
            if (self.mode not in ("idle_starvation", "interrupt_mask", "global_starvation") or not match or not self.profile or
                    self.software_time is None or int(match[1]) != self.supervisor_checks + 1):
                raise RuntimeError("Wrong starvation supervisor feed sequence")
            if self.mode == "global_starvation" and self.boots == 1 and self.stall_time is not None:
                raise RuntimeError("Global starvation did not stop the supervisor")
            self.supervisor_checks = int(match[1])
            if self.boots == 1 and self.stall_time is not None and self.sdk_trigger_time is None:
                self.post_injection_feeds.append(now)
        elif line.startswith("TEST: WDT global starvation injected"):
            if (self.mode != "global_starvation" or line != GLOBAL_INJECT or self.boots != 1 or
                    not self.profile or not self.core or self.stall_time is not None or self.supervisor_checks != 6 or
                    self.software_time is None or now - self.software_time < 20):
                raise RuntimeError("Wrong/premature/repeated global release marker")
            self.stall_time = now
        elif line.startswith(("TEST: WDT idle starvation injected", "TEST: WDT interrupt masking injected")):
            match = STARVE_INJECT.fullmatch(line)
            expected_kind = "idle starvation" if self.mode == "idle_starvation" else "interrupt masking"
            expected_policy = "interrupts=enabled" if self.mode == "idle_starvation" else "timeout_ms=300"
            matches_profile = (match and match[1] == expected_kind and int(match[2]) == self.fault_core and match[3] == expected_policy)
            if (self.mode not in ("idle_starvation", "interrupt_mask") or not matches_profile or self.boots != 1 or
                    not self.core or not self.profile or self.stall_time is not None or
                    self.supervisor_checks != 6 or self.software_time is None or now - self.software_time < 20):
                raise RuntimeError("Wrong/premature/repeated starvation injection")
            self.stall_time = now
        elif line == DROP:
            if self.mode != "task_loss" or not self.profile or not self.armed or self.drop_time is not None:
                raise RuntimeError("Wrong/repeated heartbeat injection")
            self.drop_time = now
        elif line == STALL:
            if (self.mode != "supervisor_stall" or self.boots != 1 or not self.profile or
                    not self.core or self.software_time is None or self.stall_time is not None or
                    now - self.software_time < 20):
                raise RuntimeError("Wrong/premature/repeated supervisor stall")
            self.stall_time = now
        elif line.startswith("TEST: WDT stack_free_min_bytes"):
            match = STACK.fullmatch(line)
            if self.mode == "normal" or not self.profile or self.software_time is None or not match:
                raise RuntimeError("Wrong/malformed stack evidence")
            remaining = int(match[1])
            if not 512 <= remaining <= 4096:
                raise RuntimeError("Supervisor stack headroom is insufficient/invalid")
            self.stack_seen = True
            self.stack_min = remaining if self.stack_min is None else min(self.stack_min, remaining)
        elif line.startswith("TEST: WDT task_loss"):
            match = STATE.fullmatch(line)
            if self.mode != "task_loss" or not self.profile or not match:
                raise RuntimeError("Wrong or malformed fault snapshot")
            mode, active, reports, uncertain, overflow = map(int, match.groups())
            if uncertain or overflow:
                raise RuntimeError("Fault recording is uncertain/full")
            if active:
                if (active != 1 or reports != 1 or mode != 1 or not self.fault_seen or
                        self.task_timeout_time is None):
                    raise RuntimeError("SAFE/single retained cause evidence is inconsistent")
                self.safe_times.append(now)
        elif line.startswith("TEST:"):
            raise RuntimeError("Unexpected firmware test profile")
        match = FAULT.search(line)
        if match and int(match[2]) >= 2:
            if (self.mode != "task_loss" or self.drop_time is None or self.fault_seen or
                    (int(match[1]), int(match[2]), int(match[3], 16)) != (4, 2, 0xC200)):
                raise RuntimeError("Unexpected/repeated critical or fatal fault")
            self.fault_seen = True
        match = TASK_TIMEOUT.search(line)
        if match:
            if (self.mode != "task_loss" or int(match[1]) != 0 or not self.fault_seen or
                    self.drop_time is None or self.task_timeout_time is not None or
                    now - self.drop_time < 14):
                raise RuntimeError("Unexpected/premature/repeated mission-task timeout")
            self.task_timeout_time = now

    def complete(self, now):
        ready = self.config is not None and self.resources and self.armed and self.core and self.software_time is not None
        if not ready or now - self.software_time < self.duration:
            return False
        mission = len(self.nominal_times) >= 3 and self.nominal_times[-1] - self.nominal_times[0] >= 9
        if self.mode == "normal":
            return self.boots == 1 and mission
        if self.mode == "task_loss":
            return (self.boots == 1 and self.profile and self.stack_seen and self.task_timeout_time is not None and
                    len(self.safe_times) >= 3 and self.safe_times[-1] - self.safe_times[0] >= 25)
        return (self.boots == 2 and self.profile and self.stack_seen and self.expected_panic() and
                self.panic_seen and mission and (self.mode != "idle_starvation" or bool(self.post_injection_feeds)))

    def summary(self):
        if self.mode == "task_loss":
            return ("task 0 heartbeat loss -> critical SRC_CDH/0xC200 -> SAFE; "
                    f"single cause retained across {len(self.safe_times)} successful SDK-fed checks, no reboot; "
                    f"WDT stack minimum free {self.stack_min} bytes")
        if self.mode == "supervisor_stall":
            interval = self.sdk_trigger_time - self.stall_time
            return (f"WDT_MON self-stall -> SDK timeout after {interval:.2f} s -> ESP_RST_TASK_WDT; "
                    "second boot suppresses test reinjection and resumes normal mission cycles; "
                    f"WDT stack minimum free {self.stack_min} bytes")
        if self.mode == "global_starvation":
            interval = self.sdk_trigger_time - self.stall_time
            unserved = "/".join(sorted(self.failed_global))
            return (f"dual-core IRQ-enabled starvation (running=WDT_G0/WDT_G1, unserved={unserved}) -> SDK timeout after {interval:.3f} s -> ESP_RST_TASK_WDT; "
                    f"second boot suppresses reinjection and resumes mission; WDT stack minimum free {self.stack_min} bytes")
        if self.mode in ("idle_starvation", "interrupt_mask"):
            interval = self.sdk_trigger_time - self.stall_time
            cause = (f"IDLE{self.fault_core} starvation with WDT_MON still fed -> ESP_RST_TASK_WDT" if self.mode == "idle_starvation"
                     else f"CPU{self.fault_core} IRQ mask -> ESP_RST_INT_WDT")
            return (f"{cause}; real SDK timeout after {interval:.3f} s; "
                    f"second boot suppresses test reinjection and resumes mission; WDT stack minimum free {self.stack_min} bytes")
        return "shared SDK timeout 15 s/idle mask 3/panic, normal mission cycles and no critical/fatal fault or reset"


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--mode", choices=("normal", "task_loss", *RESET_MODES), required=True)
    parser.add_argument("--fault-core", type=int, choices=(0, 1), default=0, help="Starvation fault core (default: 0)")
    parser.add_argument("--duration", type=int, default=90, help="Seconds after supervisor start (after recovery for stall test)")
    parser.add_argument("--reset", action="store_true", required=True, help="Restart board; existing RAM archive is lost")
    args = parser.parse_args()
    evidence = WatchdogEvidence(args.mode, args.duration, fault_core=args.fault_core)
    whitelist = (BOOT, "WATCHDOG:", "TEST: WDT", "TEST ERROR:", RESOURCES, CORE, NOMINAL)
    with serial.Serial() as port:
        port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
        port.dtr = port.rts = False
        port.open()
        port.reset_input_buffer()
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        started = time.monotonic()
        deadline = started + (args.duration + 175 if args.mode in RESET_MODES else args.duration + 60)
        while time.monotonic() < deadline:
            line = port.readline(1026).decode("utf-8", errors="replace").strip()
            now = time.monotonic()
            diagnostic = panic_diagnostic(line)
            if diagnostic:
                print(diagnostic, flush=True)
            try:
                evidence.observe(line, now)
            except RuntimeError:
                # A Guru line may precede its canary reason/backtrace. Keep the
                # test failed; collect only bounded, sanitized panic diagnosis.
                if diagnostic:
                    drain_until = time.monotonic() + 3
                    for _ in range(40):
                        if time.monotonic() >= drain_until:
                            break
                        following = port.readline().decode("utf-8", errors="replace")
                        detail = panic_diagnostic(following)
                        if detail:
                            print(detail, flush=True)
                        if "Backtrace:" in following:
                            break
                raise
            # Do not echo arbitrary SDK/Wi-Fi logs or credential-like content.
            if line.startswith(whitelist) or FAULT.search(line):
                print(line, flush=True)
            elif "Task watchdog got triggered" in line:
                print("SDK: task watchdog timeout observed", flush=True)
            elif evidence.mode == "global_starvation" and evidence.global_running_section:
                running = re.search(r"CPU\s+([01]):\s+(WDT_G[01])\b", line)
                if running:
                    print(f"SDK: CPU{running[1]} running {running[2]}", flush=True)
            elif evidence.sdk_entries and evidence.mode == "global_starvation":
                subscriber = re.search(r"-\s+(WDT_MON|IDLE0|IDLE1)\s+\(CPU", line)
                if subscriber:
                    print(f"SDK: {subscriber[1]} listed as unserved subscriber", flush=True)
            elif evidence.sdk_entries and "WDT_MON" in line:
                print("SDK: WDT_MON listed as unserved subscriber", flush=True)
            elif evidence.sdk_entries and f"IDLE{evidence.fault_core}" in line:
                print(f"SDK: IDLE{evidence.fault_core} listed as unserved subscriber", flush=True)
            elif "Aborting." in line:
                print("SDK: expected watchdog panic observed", flush=True)
            if evidence.complete(now):
                print("PASS: " + evidence.summary() + f" ({now - started:.1f} s observation).", flush=True)
                return
        raise RuntimeError("Watchdog evidence incomplete within bounded observation")


if __name__ == "__main__":
    main()
