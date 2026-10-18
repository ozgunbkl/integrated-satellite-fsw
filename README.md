# Integrated Satellite FSW

An embedded-C satellite flight-software development platform running on an
ESP32 with ESP-IDF and FreeRTOS. The project focuses on how power management,
attitude-control simulation, command handling, housekeeping, fault management
and data storage work together through shared operating permissions.

The ESP32 runs real tasks, queues, mutexes, watchdog integration and UDP
communication with YAMCS. Sensors, actuators, solar power and orbital dynamics
are simulated. This is a development and integration project, not
flight-qualified or flight-ready spacecraft software.

The selected one-hour active-workload test passed on 17 October 2026:
**3600.2 seconds, 120 resource reports and 720 sampled NOMINAL cycles**. The run
saved 3554 synthetic payload records and 732 HK records in 867 verified export
pairs, then explicitly stopped the payload and returned to SAFE. Export counts
include setup/drain/cleanup, not just the exact resource-observation window.
See [results, first-attempt failure and limits](docs/nominal_soak_target.md).

## Integrated subsystems

| Component | Responsibility |
| --- | --- |
| CDH | Telecommand validation, routing, telemetry generation and archive logging |
| EPS | Simulated bus/SOC/PDU state, load shedding and power-aware operation permissions |
| ADCS | B-Dot/PD demonstration, quaternion/vector math and a simplified attitude/orbit plant |
| HK | Periodic master report assembled from subsystem snapshots; configurable reporting interval |
| FDIR / system_mode | Warning processing, retained per-boot critical fault causes and guarded mode transitions |
| Archive | Bounded RAM records with CRC16 and resumable, manifest-based ground transfers |
| Payload | Mission-instrument management (OFF/STANDBY/ACTIVE/SAFE/ERROR); currently synthetic 64-byte data, not a camera driver |
| Watchdog | Seven mission heartbeats plus ESP-IDF supervisor/idle-task reset recovery |
| Persistent recovery | Small NVS boot/fault/reset journal, boot-loop detection and fresh health/ground-approval gate |
| Platform adapters | SDK-backed time/watchdog/boot identity; a separate mock HAL for experiments |

Subsystems do not independently authorize recovery. For example, voltage
recovery alone does not clear an active fault or permit a return to normal
operations. ADCS actuation and payload collection recheck current mode and
power permissions when committing an operation.

Persistent recovery keeps a small boot/reset/fault summary, not the science-data
archive. A retained fault/reset history can hold a new boot in SAFE until fresh
health evidence and explicit approval for that boot counter are accepted. That
approval does not erase history or request NOMINAL; the mode request is separate.
Storage uncertainty remains inhibited rather than automatically erasing NVS.

Stable-heartbeat task-timeout recovery has passed host, normal-build and a bounded
two-episode board test. The watchdog owner clears only its matching critical
timeout after a fresh >=30 s liveness window, never changes the global mode or
clears another fault. This does not certify subsystem repair or restart a task.
See [recovery conditions and limits](docs/watchdog_recovery.md) and
[board evidence with normal-firmware restoration](docs/watchdog_recovery_target.md).

The ADCS plant is deliberately simplified: its control effort is not calibrated
torque, its attitude quaternion is not a complete rigid-body dynamics solution,
and scheduler jitter is not converted into a measured integration step.
See [simulation contracts](docs/simulation_contracts.md) and
[subsystem safety](docs/subsystem_safety.md).

## Runtime architecture

```text
YAMCS -> UDP TC :10025 -> TC_RX -> command queue -> CMD_PROC -> router -> services
EPS / ADCS / HK ------------------------------------> master HK -> UDP TM :10015
Mission printf mirror / direct events ----------------> events -> UDP TM :10015
RAM archive <-> transfer service <-> verified HTTP/UDP ground transfer client
               system_mode + EPS permissions gate mission operations
```

ESP-IDF/CMake owns the component dependency graph. Shared mission IDs and types
are in the header-only `fsw_common` component; platform services have explicit
adapters. There is no completed STM32 port: several application services still
depend directly on FreeRTOS and ESP-IDF.

Startup first commits the boot journal, then prepares core services and the RAM archive once, creates mandatory
queues/mutexes, attempts optional network/log setup, arms supervision and then
creates mission tasks. A required service/allocation failure stops startup;
unavailable optional networking does not prevent the core tasks from starting.

### Application tasks

Configured waits are not execution-time or deadline guarantees. Stack budgets
below are ESP-IDF bytes, not upstream FreeRTOS word counts.

| Task | Priority | Stack bytes | Configured wait / role |
| --- | --- | --- | --- |
| EPS_MGMT | configMAX_PRIORITIES - 1 | 4096 | 100 ms; EPS policy |
| WDT_MON | 6 | 4096 | 5000 ms; software/SDK supervision |
| DEPLOY | 5 | 4096 | One-shot simulated 10 s + 2 s sequence; then deletes itself |
| CMD_PROC | 5 | 4096 | Up to 100 ms queue wait; validates/routes commands |
| TC_RX | 4 | 4096 | UDP receive with bounded socket waits |
| EPS_MON | 4 | 2048 | 1000 ms; CDH power checks |
| FDIR | 4 | 4096 | 1000 ms delay-until; warning processing |
| DATA_LOG | 4 | 4096 | Up to 100 ms queue wait; archive/payload service |
| ADCS_MGMT | 4 | 4096 | 100 ms; control/model update |
| TM_GEN | 3 | 4096 | 1000 ms; HK service, 5000 ms legacy archive cadence |
| WIFI_MGR | 2 | 4096 | Optional connection lifecycle |
| CMD_INJECT | 1 | 4096 | One-shot internal demonstration; then deletes itself |
| SOLAR_HARV | 1 | 4096 | 1000 ms; simplified environment |
| YLOG_TX | 1 | 3072 | Optional printf-to-event queue consumer |
| RECOVERY | 1 | 6144 | 1000 ms; boot health/journal and explicit rearm processing |
| RES_MON | 1 | 4096 | Optional diagnostics; nominal 100 ms queue sampling, 30 s reports |

SDK/internal tasks are additional. The normal build still launches the legacy
command injector: it requests NOMINAL after 5 s, sends a stub heater command
after another 10 s and requests a local archive readout after another 10 s.
Deployment completion separately records permission after its safety sequence.
That readout is not a transmitted downlink or a ground acknowledgment.
Neither deployment nor the heater command operates physical hardware.

See [startup policy](docs/startup_policy.md),
[mode policy](docs/system_modes.md), [watchdog policy](docs/watchdog_policy.md)
and [component dependencies](docs/component_dependencies.md).

The EPS environment uses a bounded 20 Wh equivalent energy store and a linear
0..9 V proxy, with measured elapsed time, 60 s sunlight/60 s shadow and a 24 W
synthetic sunlight source. These are demo assumptions, not measured spacecraft
power sizing. Payload rate commands select a real 1..10 Hz upper bound; the
one-hour test selected 1 Hz. Payload starts OFF and needs explicit INIT/START.
A future camera would need its own driver, image framing and storage sizing;
the current test does not validate image capture or camera throughput.

## Ground communication and storage

The maintained [XTCE profile](config/yamcs/xtce.xml) runs in the existing
[yamcs-ground project](https://github.com/ozgunbkl/yamcs-ground).
It is not a newly generated replacement ground application. The tested local
integration uses YAMCS 5.12.0; the web/API port (8090 in this setup) is separate
from UDP telemetry 10015 and telecommand 10025.

| Telemetry | APID | Body |
| --- | --- | --- |
| Master HK | 5 | 28 bytes: BE uptime uint32 and six float32 values |
| Archive record | 96 | Versioned metadata, payload length/CRC16 and payload |
| Archive transfer reply | 97 | Status/manifest/record envelope |
| Event | 2000 | Fixed 64-byte UTF-8/NUL-padded text; messages may be truncated |

HK engineering units are ms, V, %, degC and rad/s. The unset battery-temperature
entry is transmitted as zero without a validity bit: it is not proof of a real
temperature measurement. Current packets have no onboard absolute timestamp;
the ground preprocessor assigns generation time.

Commands use a restricted CCSDS-primary-header profile, not a claim of complete
CCSDS compliance. The UDP command wire has no authentication or end-to-end CRC.
A locally generated CRC protects the expanded internal queue representation.
Use this interface only on a trusted development network.

The maintained MDB contains 15 concrete commands plus one abstract CCSDS base:
HK interval, ADCS mode, payload lifecycle/rate, separate FDIR mode/boot approval,
legacy archive lookup and five transfer commands. The transfer flow is
BEGIN/PENDING/READ/CONFIRM/CANCEL:
exports are CRC-checked, exclusively created, fsynced and read back before the
separate explicit destructive confirmation is permitted. Appended tail records
are not removed by confirming an older prefix.

For a sustained producer, [archive_drain.py](tools/archive_drain.py) saves and
verifies two local copies before confirming each exact prefix. It is opt-in:
the default only previews command generation. Active draining requires explicit
`--send --confirm-clear` and a new output directory; ambiguous outcomes stop
without automatic confirmation retries. The two files are not independent
backup media. There is no automatic drain or archive overwrite on the ESP32;
if the 30-record archive fills, new writes are rejected and the payload enters
ERROR. The successful sustained test depended on the ground consumer keeping up.

Archive capacity is 30 records of up to 128 payload bytes. It is RAM-only:
power loss, reset and firmware upload lose the onboard archive and per-boot
fault/transfer state. Export before resetting. Files already saved on the
computer are not restored to RAM automatically. Physical power-loss durability,
cryptographic delivery and exactly-once UDP processing are not claimed.

See [wire contract](docs/tm_tc_contract.md),
[YAMCS profile](docs/yamcs_profile.md),
[archive client](docs/yamcs_archive_client.md) and
[transfer protocol](docs/archive_transfer_wire.md).

## Build and verification

For a step-by-step local demo, see the [demo walkthrough](docs/demo_walkthrough.md).
The [closure checklist](docs/release_checklist.md) separates completed board/soak
checks from documentation, source-only setup checks and publication approval.

Use PlatformIO with the ESP32/ESP-IDF environment in [platformio.ini](platformio.ini).
The platform is pinned to the tested Espressif32 6.12.0 / ESP-IDF 5.5.0 setup
(PlatformIO Core 6.2.0, Python 3.11.7 on the validated Windows machine).
A firmware build and host tests do **not** require a connected board. A missing
private Wi-Fi header also permits a build; Wi-Fi is unavailable until configured.
For a connected demo, configure credentials using [the setup guide](docs/wifi.md);
credentials, local exports and generated build output do not belong in Git.

```powershell
# Build only: does not flash or reset.
pio run -e esp32dev

# Host checks: Cygwin GCC + Python on the validated Windows setup.
# -BuildMetadata expects a current normal esp32dev build.
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_checks.ps1 -Compiler C:\cygwin64\bin\gcc.exe -Python C:\.platformio\penv\Scripts\python.exe -BuildMetadata

# Optional native MDB/command/preprocessor tests using existing ground jars/JDK.
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check_yamcs_mdb.ps1 -GroundProject C:\yamcs-ground\my-mission-control
```

If `pio` is not on PATH, use a PlatformIO terminal or its full executable path.
Native MDB checks need JDK 17+ and the existing packaged YAMCS 5.12.0 libraries;
they do not download dependencies or start a server.

Documented host coverage: **52 C groups, 53 independently compiled public headers
and 535 Python tests in 27 files**, plus 22 PowerShell graph/layout cases,
49 runner-refusal cases and 39 workflow/artifact mocks. Compile guards reject
59 watchdog, 21 startup and 17 lifecycle invalid profile combinations.
Mocks do not establish hardware timing, physical sensor behavior or universal
failure coverage. Fault-injection hardware profiles are opt-in and separate
from the normal build; the common host runner never flashes hardware.

With an already running matching YAMCS instance:

```powershell
# Read-only HTTP comparison: three HK packets, all units and a bounded event window.
python tools/check_yamcs_telemetry.py

# Serial-only resource observer: does not request reset or issue commands.
python tools/resource_monitor_check.py --port COM7 --duration 600
```

Flashing is a separate destructive-to-RAM action:
`pio run -e esp32dev -t upload --upload-port COM7`.
Do not run it until the existing archive has been exported or intentionally
discarded. Do not run Maven clean or delete the ground project's generated
runtime directory without first understanding where its database is stored.
See [YAMCS source ownership and data-loss precautions](docs/yamcs_data_safety.md):
this installation keeps persistent ground data inside `target/yamcs/yamcs-data/`.
Generated runtime configuration, ground history and verified board exports
are different things; rebuilding does not recover deleted history or exports.

## Verified results and remaining work

Documented ESP32/YAMCS checks include retained export, prefix-only confirmation,
tail preservation, duplicate receipts, cancellation/resume and controlled loss
of an accepted HTTP response. The latter is a client-side loss model, not a
physical UDP-loss or server-crash test.

All seven master HK fields/units were compared with real packets. A bounded
235-event window matched the ground events, including 96 repeated-message
pairs already present in the packet stream. Native tests cover severity mapping,
empty padded messages and repeated-event summaries.

The previous FDIR overflow was corrected with a 4096-byte stack and checked
through archive-full warnings and DEGRADED handling on the board. Resource
diagnostics now expose observed RAM, persistent task stack headroom, sampled
mission queue occupancy and tick-quantized heartbeat intervals. These are not
CPU utilization, WCET, exact queue peaks or flight-qualification results.

Separate CPU0 and CPU1 tests verified real task-watchdog reset on idle starvation
while the supervisor still fed, and interrupt-watchdog reset on IRQ masking.
Each recovered boot resumed the mission, and the package restored/checked normal
firmware. A gated, IRQ-enabled priority-24 worker pair also verified a bounded
dual-core starvation path: SDK snapshots showed both workers running and the
supervisor unserved, followed by real TWDT recovery and normal restore. The
initial reader mismatch and corrected SDK round-flag interpretation are documented;
universal deadlock/recovery coverage is not established.

- [Archive transaction evidence](docs/archive_target_tests.md)
- [HK and event evidence](docs/hk_event_target.md)
- [FDIR stack evidence](docs/fdir_stack_target.md)
- [Resource measurement scope and results](docs/resource_target.md)
- [Watchdog hardware coverage and limitations](docs/watchdog_target_tests.md)
- [CPU0/CPU1 idle-starvation / interrupt-watchdog evidence](docs/watchdog_starvation_target.md)
- [Bounded dual-core starvation evidence](docs/watchdog_global_target.md)
- [Persistent boot history and separate ground approval](docs/recovery_policy.md)
- [One-hour SAFE resource observation](docs/long_run_target.md)
- [One-hour active NOMINAL workload and retained exports](docs/nominal_soak_target.md)
- [Active roadmap](docs/roadmap.md)

The selected validation scope, including startup caller failures, watchdog
recovery, persistent boot-loop handling and the one-hour SAFE and active NOMINAL
observations, is complete in its documented bounds. Untested fault variations
are limitations, not a new mandatory scenario queue. No real sensors, embedded
AI, camera integration or completed STM32 port are in the current scope.

Historical intermittent serial-upload/history-loss failures remain documented:
later successful uploads and byte-identical NVS checks do not establish their
root cause. Do not use the disabled ROM/no-stub preservation path. See
[recovery evidence and unresolved risk](docs/recovery_target.md).

Publication is separate from test success. Private credentials, CVs, raw exports,
firmware binaries and generated logs are excluded. The old Git history contains
previously committed Wi-Fi credentials; removing them from today's tree does not
revoke them. Rotate exposed credentials before sharing and review history with
the repository owner. See the [release preflight](docs/release_preflight.md).
