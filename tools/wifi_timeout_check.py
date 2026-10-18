"""Observe a real connection timeout; only the explicit test firmware is accepted."""
import argparse
import time

import serial


BOOT = "--- Integrated Satellite FSW Initialization Started ---"
PROFILE = "TEST: Wi-Fi timeout profile active; temporary settings use RAM."
TIMEOUT = "Wi-Fi did not connect within timeout"
CONTINUE = "Wi-Fi not ready at startup (ESP_ERR_TIMEOUT); core startup continues, log mirror skipped."
CORE = "MAIN: Core tasks created. Network at startup: unavailable; log mirror: not started."
NOMINAL = "TM GEN: -- Running NOMINAL Mission Cycle --"
MODE = "SYSTEM MODE: INITIAL_BOOT -> NOMINAL."
RESOURCES = "MAIN: Required queues and EPS mutex ready before Wi-Fi/log initialization."
STACK = "MAIN: Network stack initialized; socket access enabled."
RECEIVER = "TC_RX: listening on UDP 10025"
TASKS = {
    RECEIVER,
    "WATCHDOG: Software Watchdog initialized.",
    "TC PROC: Task initialized.",
    "CDH EPS Monitor: Now linked to EPS Subsystem Telemetry.",
    "DATA LOGGER: Task initialized,",
    "ADCS TASK: Started. Startup state prepared by app_main.",
    "TM Generator Task initialized and running.",
}


class TimeoutEvidence:
    """Fail closed on IP, reboot, panic, or incomplete/out-of-order evidence."""

    def __init__(self):
        self.boot_seen = False
        self.resources_seen = False
        self.stack_seen = False
        self.profile_time = None
        self.timeout_seen = False
        self.continue_seen = False
        self.core_seen = False
        self.mode_seen = False
        self.tasks = set()
        self.nominal_times = []

    def observe(self, line, now):
        if any(marker in line for marker in ("Guru Meditation", "Backtrace:", "abort()",
                                             "CRITICAL FAILURE", "Telemetry Queue Full")):
            raise RuntimeError("Board failure during the offline observation")
        if "ESP32 IP:" in line or "MAIN: Wi-Fi connected." in line:
            raise RuntimeError("Wi-Fi unexpectedly connected during the timeout test")
        if BOOT in line:
            if self.boot_seen:
                raise RuntimeError("Unexpected additional firmware boot")
            self.boot_seen = True
        elif RESOURCES in line:
            if not self.boot_seen or self.resources_seen:
                raise RuntimeError("Required resource evidence outside the observed boot")
            self.resources_seen = True
        elif STACK in line:
            if not self.resources_seen or self.stack_seen:
                raise RuntimeError("Network stack evidence repeated or before resource readiness")
            self.stack_seen = True
        elif PROFILE in line:
            if not self.stack_seen or self.profile_time is not None:
                raise RuntimeError("Test profile repeated or before resource readiness")
            self.profile_time = now
        elif TIMEOUT in line:
            if self.profile_time is None or now - self.profile_time < 14.0 or self.timeout_seen:
                raise RuntimeError("Missing, premature, or repeated real timeout evidence")
            self.timeout_seen = True
        elif CONTINUE in line:
            if not self.timeout_seen:
                raise RuntimeError("Core continuation reported before connection timeout")
            self.continue_seen = True
        elif line.startswith("MAIN: Core tasks created."):
            if line != CORE or not self.continue_seen or self.core_seen:
                raise RuntimeError("Incorrect network/log status or repeated core readiness")
            self.core_seen = True
        elif MODE in line:
            self.mode_seen = True
        elif NOMINAL in line:
            if not self.core_seen or not self.mode_seen or self.tasks != TASKS:
                raise RuntimeError("NOMINAL telemetry before complete offline startup evidence")
            self.nominal_times.append(now)
        else:
            for marker in TASKS:
                if marker in line:
                    if not self.continue_seen:
                        raise RuntimeError("Consumer task started before timeout continuation")
                    self.tasks.add(marker)
        return (len(self.nominal_times) >= 3 and
                self.nominal_times[-1] - self.nominal_times[0] >= 9.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--reset", action="store_true", required=True,
                        help="Restart board; current RAM archive will be lost")
    args = parser.parse_args()
    evidence = TimeoutEvidence()
    with serial.Serial() as port:
        port.port = args.port
        port.baudrate = 115200
        port.timeout = 0.2
        port.dtr = False
        port.rts = False
        port.open()
        port.reset_input_buffer()
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            passed = evidence.observe(line, time.monotonic())
            # Print only known evidence; do not echo arbitrary Wi-Fi/credential logs.
            markers = (BOOT, RESOURCES, STACK, PROFILE, TIMEOUT, CONTINUE, CORE, MODE, NOMINAL, *sorted(TASKS))
            for marker in markers:
                if marker in line:
                    print(marker, flush=True)
                    break
            if passed:
                print("PASS: real Wi-Fi connection timeout, no IP, log mirror skipped; "
                      "stack ready and TC listening; core tasks and 3 NOMINAL cycles continue "
                      "without reboot/panic/watchdog failure.", flush=True)
                return
        raise RuntimeError("Incomplete timeout/profile/core/NOMINAL evidence within 65 seconds")


if __name__ == "__main__":
    main()
