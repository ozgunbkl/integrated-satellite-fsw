"""Bounded Wi-Fi interface-fault or real reconnect/soak observation; reset clears RAM."""
import argparse
import re
import time
import serial
from wifi_timeout_check import BOOT, NOMINAL, TASKS

ALLOC = "TEST: Wi-Fi station allocation failure injected; real heap remains intact."
LIFECYCLE = "TEST: Wi-Fi lifecycle profile active; late connection and 3 real disconnects."
OFFLINE = "MAIN: Core tasks created. Network at startup: unavailable; log mirror: not started."
RESTORED = "TEST: valid Wi-Fi configuration restored in RAM after late startup."
LOG_READY = "WIFI: YAMCS log mirror started after IP readiness."
HEALTH = re.compile(r"WIFI: health connected=([01]) attempts=(\d+) heap=(\d+) min_heap=(\d+) stack_free_bytes=(\d+)$")


class WifiEvidence:
    def __init__(self, mode, duration=600):
        if mode not in ("interface", "lifecycle") or duration < 300:
            raise ValueError("Choose interface/lifecycle; soak duration must be at least 300 seconds")
        self.mode, self.duration = mode, duration
        self.boot_at = None
        self.profile = self.core = self.timeout = self.allocation_error = False
        self.stack = self.event_loop = self.resources = False
        self.restored = self.log_ready = False
        self.tasks, self.nominals, self.samples = set(), [], []
        self.core_cycles = []
        self.modes = set()
        self.ips = self.requests = self.losses = 0

    def observe(self, line, now):
        if any(marker in line for marker in ("Guru Meditation", "Backtrace:", "abort()", "TEST ERROR:",
                                             "CRITICAL FAILURE", "Telemetry Queue Full", "task_wdt:", "Watchdog Timeout")):
            raise RuntimeError("Firmware/test failure during Wi-Fi observation")
        if BOOT in line:
            if self.boot_at is not None:
                raise RuntimeError("Unexpected firmware restart")
            self.boot_at = now
        if "MAIN: Required queues and EPS mutex ready before Wi-Fi/log initialization." in line:
            if self.boot_at is None or self.resources:
                raise RuntimeError("Wrong required-resource initialization order")
            self.resources = True
        if "MAIN: Network stack initialized; socket access enabled." in line:
            if not self.resources or self.stack:
                raise RuntimeError("Wrong network stack initialization order")
            self.stack = True
        if "MAIN: Wi-Fi event loop created before interface/driver initialization." in line:
            if not self.stack or self.event_loop:
                raise RuntimeError("Wrong event-loop initialization order")
            self.event_loop = True
        if ALLOC in line or LIFECYCLE in line:
            expected = ALLOC if self.mode == "interface" else LIFECYCLE
            if expected not in line or self.profile or not self.event_loop:
                raise RuntimeError("Wrong/repeated/out-of-order Wi-Fi test profile")
            self.profile = True
        if "TEST: NVS init failure" in line or "TEST: Network stack init failure" in line or "TEST: Wi-Fi event loop failure" in line:
            raise RuntimeError("Unexpected early startup fault profile")
        if "Wi-Fi startup failed at station interface (ESP_FAIL)" in line:
            self.allocation_error = True
        if "Wi-Fi did not connect within timeout" in line:
            if self.mode != "lifecycle" or not self.profile or self.ips:
                raise RuntimeError("Invalid timeout evidence")
            if now - self.boot_at < 14:
                raise RuntimeError("Timeout was not the real 15-second wait")
            self.timeout = True
        if line.startswith("MAIN: Core tasks created."):
            if line != OFFLINE or not self.profile or self.core:
                raise RuntimeError("Wrong startup snapshot or repeated core creation")
            if self.mode == "interface" and not self.allocation_error:
                raise RuntimeError("Core continuation before interface error")
            if self.mode == "lifecycle" and not self.timeout:
                raise RuntimeError("Core continuation before timeout")
            self.core = True
        if RESTORED in line:
            if self.mode != "lifecycle" or not self.core or self.restored:
                raise RuntimeError("Invalid late configuration restore")
            self.restored = True
        if "MAIN: Wi-Fi connected. ESP32 IP:" in line:
            if self.mode != "lifecycle" or not self.restored:
                raise RuntimeError("IP before expected late restoration or during interface fault")
            self.ips += 1
        if LOG_READY in line:
            if self.mode != "lifecycle" or self.ips == 0 or self.log_ready:
                raise RuntimeError("Invalid/duplicated late log startup")
            self.log_ready = True
        match = re.fullmatch(r"TEST: real station disconnect ([123])/3 requested\.", line)
        if match:
            if self.mode != "lifecycle" or not self.log_ready or int(match[1]) != self.requests + 1:
                raise RuntimeError("Invalid real disconnect sequence")
            self.requests += 1
        if "WIFI: link unavailable; transmissions paused." in line:
            self.losses += 1
        for marker in TASKS:
            if marker in line:
                if not self.core and marker != "TC_RX: listening on UDP 10025":
                    # Task prints may precede the final Core tasks created summary.
                    if not self.profile:
                        raise RuntimeError("Core consumer before startup policy")
                self.tasks.add(marker)
        if NOMINAL in line:
            if not self.core or self.tasks != TASKS:
                raise RuntimeError("NOMINAL before complete core evidence")
            self.nominals.append(now)
            self.core_cycles.append(now)
        wait_mode = re.fullmatch(r"TM GEN: Waiting for MODE_NOMINAL\. Current mode: (\d+)", line)
        if wait_mode:
            mode = int(wait_mode[1])
            if mode not in (0, 1, 3, 4, 5):
                raise RuntimeError("Invalid system mode while observing core liveness")
            if not self.core:
                # xTaskCreate can run TM_GEN before app_main prints its summary.
                # Require the offline policy already observed, but don't count it
                # toward the qualified long-running core observation yet.
                if not self.profile or (self.mode == "lifecycle" and not self.timeout):
                    raise RuntimeError("Core waiting cycle before startup policy")
                return False
            if self.tasks != TASKS:
                raise RuntimeError("Core waiting cycle before complete task evidence")
            self.modes.add(mode)
            self.core_cycles.append(now)
        if len(self.core_cycles) >= 2 and self.core_cycles[-1] - self.core_cycles[-2] > 12:
            raise RuntimeError("Core telemetry task heartbeat gap exceeded 12 seconds")
        match = HEALTH.fullmatch(line)
        if match:
            values = tuple(map(int, match.groups()))
            if any(value <= 0 for value in values[2:]):
                raise RuntimeError("Invalid heap/stack health sample")
            self.samples.append((now, values))
        if self.mode == "interface":
            if self.ips or self.log_ready or "WIFI: reconnect manager running" in line:
                raise RuntimeError("Unexpected Wi-Fi start after interface creation failure")
            return len(self.nominals) >= 3 and self.nominals[-1] - self.nominals[0] >= 9
        if self.boot_at is None or now - self.boot_at < self.duration:
            return False
        if (not self.log_ready or self.requests != 3 or self.ips < 4 or self.losses < 3 or
                len(self.nominals) < 3 or len(self.core_cycles) < 40 or
                self.core_cycles[-1] - self.core_cycles[0] < self.duration - 40):
            raise RuntimeError("Incomplete late-IP/reconnect/continuous-core evidence at soak deadline")
        stable = [values for timestamp, values in self.samples
                  if timestamp - self.boot_at >= 250 and values[0] == 1]
        if len(stable) < 2:
            raise RuntimeError("Too few post-reconnect connected heap/stack samples")
        # A bounded stability observation, not a proof against every slow leak.
        if stable[0][2] - stable[-1][2] > 8192:
            raise RuntimeError("Connected heap dropped over 8 KiB after reconnects")
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--mode", choices=("interface", "lifecycle"), required=True)
    parser.add_argument("--duration", type=int, default=600)
    parser.add_argument("--reset", action="store_true", required=True)
    args = parser.parse_args()
    evidence = WifiEvidence(args.mode, args.duration)
    with serial.Serial() as port:
        port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
        port.dtr = port.rts = False
        port.open()
        port.reset_input_buffer()
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        started = time.monotonic()
        deadline = started + (args.duration + 40 if args.mode == "lifecycle" else 60)
        next_progress = started
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            now = time.monotonic()
            previous_modes = set(evidence.modes)
            passed = evidence.observe(line, now)
            if evidence.modes != previous_modes:
                print(f"CORE MODE: live telemetry task observed in mode(s) {sorted(evidence.modes)}; NOMINAL is mode 2, CRITICAL is mode 5.", flush=True)
            if line.startswith(("TEST: Wi-Fi", "TEST: valid Wi-Fi", "TEST: real station", "WIFI: health", "WIFI: link unavailable")):
                print(line, flush=True)
            if "MAIN: Wi-Fi connected." in line:
                print(f"IP readiness observed #{evidence.ips}", flush=True)
            if LOG_READY in line:
                print(LOG_READY, flush=True)
            if now >= next_progress:
                print(f"OBSERVE: elapsed={int(now-started)}s core_cycles={len(evidence.core_cycles)} NOMINAL={len(evidence.nominals)} modes={sorted(evidence.modes)} IP={evidence.ips} disconnects={evidence.requests}/3", flush=True)
                next_progress = now + 30
            if passed:
                print(f"PASS: {args.mode}; {len(evidence.core_cycles)} core cycles ({len(evidence.nominals)} NOMINAL), modes={sorted(evidence.modes)}, IP acquisitions={evidence.ips}, real disconnects={evidence.requests}.", flush=True)
                if evidence.samples:
                    heaps = [sample[1][2] for sample in evidence.samples]
                    stacks = [sample[1][4] for sample in evidence.samples]
                    print(f"MEASURED: free_heap_range={min(heaps)}..{max(heaps)} bytes; WIFI_MGR minimum stack headroom={min(stacks)} bytes.", flush=True)
                return
        raise RuntimeError("Incomplete Wi-Fi/core evidence within the observation window")


if __name__ == "__main__":
    main()
