"""Observe injected NVS-init failure; never corrupts or erases the real NVS."""
import argparse
import time

import serial

from wifi_timeout_check import (BOOT, CORE, MODE, NOMINAL, PROFILE as WIFI_PROFILE,
                                RECEIVER, RESOURCES, STACK, TASKS as WIFI_TASKS, TIMEOUT)

PROFILE = "TEST: NVS init failure injected (ESP_ERR_NVS_NO_FREE_PAGES); real NVS init not called."
ERROR = "NVS initialization failed (ESP_ERR_NVS_NO_FREE_PAGES); Wi-Fi skipped; automatic NVS erase disabled."
CONTINUE = "Wi-Fi not ready at startup (ESP_ERR_NVS_NO_FREE_PAGES); core startup continues, log mirror skipped."
SKIPPED = "TC_RX: network stack unavailable; receiver skipped."
TASKS = WIFI_TASKS - {RECEIVER}
FAULT_PROFILES = ("TEST: NVS init failure injected", "TEST: Network stack init failure injected",
                  "TEST: Wi-Fi event loop failure injected")
INIT_ERRORS = ("NVS initialization failed (", "Network stack initialization failed (",
               "Wi-Fi event loop initialization failed (")


class StartupFailureEvidence:
    """Shared evidence parser for an explicit startup fault, not real SDK recovery."""

    def __init__(self, *, profile, error, continuation, stack_ready=False, forbidden_markers=()):
        self.profile = profile
        self.error = error
        self.continuation = continuation
        self.stack_ready = stack_ready
        self.forbidden_markers = forbidden_markers
        self.expected_tasks = WIFI_TASKS if stack_ready else TASKS
        self.boot_seen = False
        self.resources_seen = False
        self.stack_seen = False
        self.profile_seen = False
        self.error_seen = False
        self.continue_seen = False
        self.skipped_seen = False
        self.core_seen = False
        self.mode_seen = False
        self.tasks = set()
        self.nominal_times = []

    def observe(self, line, now):
        if any(marker in line for marker in ("Guru Meditation", "Backtrace:", "abort()",
                                             "CRITICAL FAILURE", "Telemetry Queue Full", "TEST ERROR:")):
            raise RuntimeError("Board/test failure during the startup-error observation")
        if any(marker in line for marker in ("ESP32 IP:", "MAIN: Wi-Fi connected.",
                                             WIFI_PROFILE, TIMEOUT, "TX ok #", *self.forbidden_markers)):
            raise RuntimeError("Unexpected network/profile/success evidence after injected startup failure")
        if not self.stack_ready and any(marker in line for marker in (STACK, RECEIVER, "UDP socket ready")):
            raise RuntimeError("Socket infrastructure enabled after early startup failure")
        if BOOT in line:
            if self.boot_seen:
                raise RuntimeError("Unexpected additional firmware boot")
            self.boot_seen = True
        elif RESOURCES in line:
            if not self.boot_seen or self.resources_seen:
                raise RuntimeError("Required resources repeated or outside the observed boot")
            self.resources_seen = True
        elif STACK in line:
            if not self.resources_seen or self.stack_seen:
                raise RuntimeError("Stack readiness repeated or before required resources")
            self.stack_seen = True
        elif any(marker in line for marker in FAULT_PROFILES):
            if (self.profile not in line or not self.resources_seen or self.profile_seen or
                    (self.stack_ready and not self.stack_seen)):
                raise RuntimeError("Wrong, repeated or out-of-order startup injection profile")
            self.profile_seen = True
        elif any(marker in line for marker in INIT_ERRORS):
            if self.error not in line or not self.profile_seen or self.error_seen:
                raise RuntimeError("Missing/incorrect startup policy or error evidence")
            self.error_seen = True
        elif self.continuation in line:
            if not self.error_seen or self.continue_seen:
                raise RuntimeError("Core continuation repeated or before startup failure evidence")
            self.continue_seen = True
        elif SKIPPED in line:
            if self.stack_ready:
                raise RuntimeError("TC incorrectly skipped despite a ready network stack")
            if not self.continue_seen or self.skipped_seen:
                raise RuntimeError("TC skip repeated or before startup continuation")
            self.skipped_seen = True
        elif line.startswith("MAIN: Core tasks created."):
            if line != CORE or not self.continue_seen or self.core_seen:
                raise RuntimeError("Incorrect network/log status or repeated core readiness")
            self.core_seen = True
        elif MODE in line:
            self.mode_seen = True
        elif NOMINAL in line:
            network_evidence = self.stack_seen if self.stack_ready else self.skipped_seen
            if (not self.core_seen or not self.mode_seen or not network_evidence or
                    self.tasks != self.expected_tasks):
                raise RuntimeError("NOMINAL before complete startup-error/core/network-state evidence")
            self.nominal_times.append(now)
        else:
            for marker in self.expected_tasks:
                if marker in line:
                    if not self.continue_seen:
                        raise RuntimeError("Consumer started before startup failure continuation")
                    self.tasks.add(marker)
        return (len(self.nominal_times) >= 3 and
                self.nominal_times[-1] - self.nominal_times[0] >= 9.0)


class NvsFailureEvidence(StartupFailureEvidence):
    def __init__(self):
        super().__init__(profile=PROFILE, error=ERROR, continuation=CONTINUE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--reset", action="store_true", required=True,
                        help="Restart ESP32; current RAM archive records will be lost")
    args = parser.parse_args()
    evidence = NvsFailureEvidence()
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
        deadline = time.monotonic() + 50
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            passed = evidence.observe(line, time.monotonic())
            # Do not echo arbitrary Wi-Fi/settings logs.
            for marker in (BOOT, RESOURCES, PROFILE, ERROR, CONTINUE, SKIPPED,
                           CORE, MODE, NOMINAL, *sorted(TASKS)):
                if marker in line:
                    print(marker, flush=True)
                    break
            if passed:
                print("PASS: injected NVS init failure without real init/erase/retry; "
                      "stack stays disabled and TC is skipped; core tasks and 3 NOMINAL cycles "
                      "continue without reboot/panic/watchdog failure.", flush=True)
                return
        raise RuntimeError("Incomplete NVS-profile/no-erase/network-disabled/core evidence within 50 seconds")


if __name__ == "__main__":
    main()
