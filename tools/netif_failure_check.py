"""Observe an injected esp_netif_init error; no real SDK failure/heap exhaustion."""
import argparse
import time

import serial

from nvs_failure_check import (BOOT, CORE, MODE, NOMINAL, RESOURCES, SKIPPED, TASKS,
                               StartupFailureEvidence)

PROFILE = "TEST: Network stack init failure injected (ESP_FAIL); real esp_netif_init not called."
ERROR = "Network stack initialization failed (ESP_FAIL); Wi-Fi skipped; socket access stays disabled."
CONTINUE = "Wi-Fi not ready at startup (ESP_FAIL); core startup continues, log mirror skipped."


class NetifFailureEvidence(StartupFailureEvidence):
    def __init__(self):
        super().__init__(profile=PROFILE, error=ERROR, continuation=CONTINUE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--reset", action="store_true", required=True,
                        help="Restart ESP32; current RAM archive records will be lost")
    args = parser.parse_args()
    evidence = NetifFailureEvidence()
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
            for marker in (BOOT, RESOURCES, PROFILE, ERROR, CONTINUE, SKIPPED,
                           CORE, MODE, NOMINAL, *sorted(TASKS)):
                if marker in line:
                    print(marker, flush=True)
                    break
            if passed:
                print("PASS: injected esp_netif_init error returns without retry/deinit; "
                      "readiness stays disabled and TC is skipped; core tasks and 3 NOMINAL "
                      "cycles continue without reboot/panic/watchdog failure.", flush=True)
                return
        raise RuntimeError("Incomplete network-init profile/error/disabled/core evidence within 50 seconds")


if __name__ == "__main__":
    main()
