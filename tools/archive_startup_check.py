"""Observe startup readiness before consumer tasks; explicit --reset clears RAM records."""
import argparse
import ipaddress
import re
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--services", action="store_true",
                        help="Also require HK/payload readiness and payload OFF before consumer tasks")
    parser.add_argument("--adcs", action="store_true",
                        help="Also require one ADCS init in DETUMBLE before tasks (implies --services)")
    parser.add_argument("--resources", action="store_true",
                        help="Also require queues/EPS mutex readiness before Wi-Fi/tasks (implies --adcs)")
    parser.add_argument("--network", action="store_true",
                        help="Also require stack readiness before IP/tasks and TC listening (implies --resources)")
    parser.add_argument("--event-loop", action="store_true",
                        help="Also require successful event-loop creation before IP/tasks (implies --network)")
    parser.add_argument("--reset", action="store_true", required=True,
                        help="Restart ESP32; existing RAM archive records will be lost")
    args = parser.parse_args()
    args.network = args.network or args.event_loop
    args.resources = args.resources or args.network
    args.adcs = args.adcs or args.resources
    args.services = args.services or args.adcs
    ready_marker = "MAIN: RAM archive initialized before task creation."
    services_prefix = "MAIN: HK and payload initialized before task creation"
    services_marker = services_prefix + " (payload state=0)."
    adcs_init_marker = "ADCS: Initialized in DETUMBLE mode."
    adcs_prefix = "MAIN: ADCS initialized before task creation"
    adcs_marker = adcs_prefix + " (mode=1)."
    resources_marker = "MAIN: Required queues and EPS mutex ready before Wi-Fi/log initialization."
    network_marker = "MAIN: Network stack initialized; socket access enabled."
    event_marker = "MAIN: Wi-Fi event loop created before interface/driver initialization."
    task_markers = {"TC PROC: Task initialized.", "DATA LOGGER: Task initialized,"}
    if args.services:
        task_markers.add("TM Generator Task initialized and running.")
    if args.adcs:
        task_markers.add("ADCS TASK: Started. Startup state prepared by app_main.")
    if args.network:
        task_markers.add("TC_RX: listening on UDP 10025")
    seen_tasks = set()
    ready_count = 0
    services_count = 0
    adcs_init_count = 0
    adcs_count = 0
    resources_count = 0
    network_count = 0
    event_count = 0
    board_ip = None
    boot_seen = False

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

        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            if any(marker in line for marker in ("Guru Meditation", "Backtrace:", "abort()")):
                raise RuntimeError("Board failed during startup")
            if "TEST: Wi-Fi timeout profile active" in line:
                raise RuntimeError("Timeout test firmware is installed; restore esp32dev first")
            if "TEST: NVS init failure injected" in line or "TEST ERROR:" in line:
                raise RuntimeError("NVS fault test firmware is installed or failed; restore esp32dev first")
            if "TEST: Network stack init failure injected" in line:
                raise RuntimeError("Network stack fault test firmware is installed; restore esp32dev first")
            if "TEST: Wi-Fi event loop failure injected" in line:
                raise RuntimeError("Event-loop fault test firmware is installed; restore esp32dev first")
            if "TEST: Wi-Fi lifecycle profile active" in line or "TEST: Wi-Fi station allocation failure injected" in line:
                raise RuntimeError("Wi-Fi lifecycle/interface test firmware is installed; restore esp32dev first")
            if line.startswith("MAIN: Core tasks created."):
                print(line, flush=True)
            if "Integrated Satellite FSW Initialization Started" in line:
                if boot_seen:
                    raise RuntimeError("Unexpected additional boot during startup")
                boot_seen = True
                print("BOOT: firmware initialization started", flush=True)
            if ready_marker in line:
                ready_count += 1
                if not boot_seen or ready_count != 1:
                    raise RuntimeError("Archive readiness was repeated or outside the observed boot")
                print(ready_marker, flush=True)
            if args.services and services_prefix in line:
                services_count += 1
                # PL_STATE_OFF is 0; the firmware prints the actual telemetry state.
                if line != services_marker or ready_count != 1 or services_count != 1:
                    raise RuntimeError("HK/payload readiness missing, repeated, out of order or not OFF")
                print(services_marker, flush=True)
            if args.adcs and adcs_init_marker in line:
                adcs_init_count += 1
                if services_count != 1 or adcs_init_count != 1:
                    raise RuntimeError("ADCS initialization repeated or before core service readiness")
                print(adcs_init_marker, flush=True)
            if args.adcs and adcs_prefix in line:
                adcs_count += 1
                # ADCS_MODE_DETUMBLE is 1; the firmware prints the actual mode.
                if line != adcs_marker or adcs_init_count != 1 or adcs_count != 1:
                    raise RuntimeError("ADCS readiness missing, repeated, out of order or not DETUMBLE")
                print(adcs_marker, flush=True)
            if args.resources and resources_marker in line:
                resources_count += 1
                if adcs_count != 1 or resources_count != 1:
                    raise RuntimeError("Required resource readiness repeated or outside startup order")
                print(resources_marker, flush=True)
            if args.network and network_marker in line:
                network_count += 1
                if resources_count != 1 or network_count != 1:
                    raise RuntimeError("Network stack readiness repeated or before required resources")
                print(network_marker, flush=True)
            if args.event_loop and event_marker in line:
                event_count += 1
                if network_count != 1 or event_count != 1:
                    raise RuntimeError("Event-loop readiness repeated or before stack readiness")
                print(event_marker, flush=True)
            for marker in task_markers:
                if marker in line:
                    if (ready_count != 1 or (args.services and services_count != 1) or
                            (args.adcs and (adcs_count != 1 or adcs_init_count != 1)) or
                            (args.resources and resources_count != 1) or
                            (args.network and network_count != 1) or
                            (args.event_loop and event_count != 1)):
                        raise RuntimeError("Consumer started before required service initialization")
                    seen_tasks.add(marker)
                    print(marker + " required services already ready", flush=True)
            match = re.search(r"ESP32 IP: (\d+\.\d+\.\d+\.\d+)", line)
            if match:
                if args.resources and resources_count != 1:
                    raise RuntimeError("Wi-Fi connected before required resources were ready")
                if args.network and network_count != 1:
                    raise RuntimeError("Wi-Fi connected before network stack readiness")
                if args.event_loop and event_count != 1:
                    raise RuntimeError("Wi-Fi connected before event-loop readiness")
                board_ip = str(ipaddress.IPv4Address(match.group(1)))
            if "TM GEN: -- Running NOMINAL Mission Cycle --" in line:
                if (not boot_seen or ready_count != 1 or seen_tasks != task_markers or
                        board_ip is None or (args.services and services_count != 1) or
                        (args.adcs and (adcs_count != 1 or adcs_init_count != 1)) or
                        (args.resources and resources_count != 1) or
                        (args.network and network_count != 1) or
                        (args.event_loop and event_count != 1)):
                    raise RuntimeError("Incomplete startup evidence before NOMINAL telemetry")
                # Give Data Logger time to consume the first telemetry record.
                time.sleep(0.5)
                services = "archive/HK/payload ready once, payload OFF" if args.services else "archive ready once"
                if args.adcs:
                    services += ", ADCS ready once in DETUMBLE"
                if args.resources:
                    services += ", required queues/EPS mutex ready (before Wi-Fi)"
                if args.network:
                    services += ", network stack ready and TC listening"
                if args.event_loop:
                    services += ", event loop created once before Wi-Fi/tasks"
                print(f"PASS: {services} before {len(task_markers)} consumers; NOMINAL, IP={board_ip}",
                      flush=True)
                return
        raise RuntimeError("Timed out observing archive initialization, task startup and NOMINAL")


if __name__ == "__main__":
    main()
