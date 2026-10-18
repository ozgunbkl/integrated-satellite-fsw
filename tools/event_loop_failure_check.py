"""Observe an injected event-loop error with a real, ready TCP/IP stack."""
import argparse
import time

import serial

from nvs_failure_check import BOOT, CORE, MODE, NOMINAL, RESOURCES, StartupFailureEvidence
from wifi_timeout_check import RECEIVER, STACK, TASKS

PROFILE = "TEST: Wi-Fi event loop failure injected (ESP_ERR_NO_MEM); real event loop creation not called."
ERROR = "Wi-Fi event loop initialization failed (ESP_ERR_NO_MEM); Wi-Fi skipped; network stack stays ready."
CONTINUE = "Wi-Fi not ready at startup (ESP_ERR_NO_MEM); core startup continues, log mirror skipped."
EVENT_READY = "MAIN: Wi-Fi event loop created before interface/driver initialization."


class EventLoopFailureEvidence(StartupFailureEvidence):
    def __init__(self):
        super().__init__(profile=PROFILE, error=ERROR, continuation=CONTINUE,
                         stack_ready=True, forbidden_markers=(EVENT_READY,))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--reset", action="store_true", required=True,
                        help="Restart ESP32; current RAM archive records will be lost")
    args = parser.parse_args()
    evidence = EventLoopFailureEvidence()
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
            # Print known evidence only, not arbitrary network/settings logs.
            for marker in (BOOT, RESOURCES, STACK, PROFILE, ERROR, CONTINUE,
                           CORE, MODE, NOMINAL, *sorted(TASKS)):
                if marker in line:
                    print(marker, flush=True)
                    break
            if passed:
                print("PASS: injected event-loop error returns without retry/delete; real stack "
                      "stays ready, TC listens without IP, Wi-Fi/log mirror are skipped; "
                      "3 NOMINAL cycles continue without reboot/panic/watchdog failure.", flush=True)
                return
        raise RuntimeError("Incomplete event-loop error/ready-stack/TC/core evidence within 50 seconds")


if __name__ == "__main__":
    main()
