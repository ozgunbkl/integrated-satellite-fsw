/* Real EPS/ADCS/payload/mode/FDIR; controlled sensors, fake archive, no hardware I/O. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "eps_service.h"
#include "eps_telemetry.h"
#include "eps_commands.h"
#include "adcs_service.h"
#include "adcs_units.h"
#include "fdir_service.h"
#include "adcs_sim.h"
#include "payload_service.h"
#include "archive_service.h"
#include "commands.h"
#include "watchdog.h"
#include "time_service.h"
#include "yamcs_interface.h"
#include "freertos/task.h"

void vEPS_Manager_Task(void *argument);
void vADCS_Manager_Task(void *argument);
void vDeploymentTask(void *argument);
bool __real_SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context);
bool __real_SIM_ApplyBoost(float distance);
Vector3_t __real_SIM_ReadGyroscope(void);
void __real_SIM_UpdatePhysics(Vector3_t command, float dt);
void __real_FDIR_ReportFault(FaultReport_t report);

typedef struct { bool held; } MockMutex;
static MockMutex locks[5]; // mode, FDIR, EPS data, ADCS state, payload state.
static unsigned allocating;
EPS_Telemetry_t g_eps_telemetry;
SemaphoreHandle_t xEPSDataMutex = &locks[2];
static MockMutex *fail_lock;
static bool fail_allocation, external_writer;
static unsigned guard_inject_countdown;
static unsigned post_guard_inject_countdown;
static SystemMode_t injected_mode = MODE_EMERGENCY;
static bool during_generation, during_submission;
static unsigned callbacks, samples, writes, faults, boosts, physics, pets, delays;
static uint64_t sample_now;
static bool sample_manual;
static Vector3_t gyro = {1, 0, 0}, last_physics_command;
static unsigned mag_samples;
static float altitude = 480.0f;
static ArchiveStatus_t archive_result = ARCHIVE_OK;
static char logs[65536], last_event[128];
static size_t log_length;
static jmp_buf task_exit, abort_exit;
static bool abort_expected;
static void (*running_task)(void *);
static unsigned deployment_waits, deployment_deletes;

int __wrap_printf(const char *format, ...) {
    assert(!locks[0].held);
    if (!external_writer) assert(!locks[2].held && !locks[3].held && !locks[4].held);
    va_list args;
    va_start(args, format);
    int count = vsnprintf(logs + log_length, sizeof(logs) - log_length, format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(logs) - log_length);
    log_length += (size_t)count;
    return count;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (fail_allocation) return NULL;
    assert(allocating < 5 && allocating != 2);
    return &locks[allocating];
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
    MockMutex *mutex = handle;
    assert(mutex >= locks && mutex < locks + 5 && !mutex->held);
    assert(ticks == portMAX_DELAY || ticks == pdMS_TO_TICKS(10));
    if (mutex == fail_lock) { fail_lock = NULL; return pdFALSE; }
    if (mutex == &locks[2]) assert(!locks[0].held);
    if (mutex == &locks[3] || mutex == &locks[4]) assert(!locks[0].held && !locks[2].held);
    mutex->held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    MockMutex *mutex = handle;
    assert(mutex->held);
    mutex->held = false;
    return pdTRUE;
}
void __wrap_abort(void) {
    assert(abort_expected);
    longjmp(abort_exit, 1);
}

typedef struct { SystemModeAction_t action; void *context; } Proxy;
static void proxy_action(SystemMode_t mode, void *context) {
    Proxy *proxy = context;
    assert(locks[0].held && locks[2].held);
    proxy->action(mode, proxy->context);
}
bool __wrap_SYSTEM_MODE_WithCurrent(SystemModeAction_t action, void *context) {
    assert(locks[2].held && !locks[0].held);
    if (guard_inject_countdown && --guard_inject_countdown == 0) {
        // Independent writer immediately before the authorization acquires mode lock.
        external_writer = true;
        assert(SYSTEM_MODE_Set(injected_mode));
        external_writer = false;
    }
    Proxy proxy = {.action = action, .context = context};
    bool checked = __real_SYSTEM_MODE_WithCurrent(proxy_action, &proxy);
    if (post_guard_inject_countdown && --post_guard_inject_countdown == 0) {
        external_writer = true;
        assert(SYSTEM_MODE_Set(injected_mode));
        external_writer = false;
    }
    return checked;
}

Vector3_t __wrap_SIM_ReadGyroscope(void) { return gyro; }
Vector3_t __wrap_SIM_ReadMagnetometer(void) {
    ++mag_samples;
    return (Vector3_t){20.0f + mag_samples * 0.5f, 10, 40};
}
float __wrap_SIM_GetAltitude(void) { return altitude; }
bool __wrap_SIM_ApplyBoost(float distance) {
    assert(locks[0].held && locks[2].held && locks[3].held);
    ++boosts;
    return __real_SIM_ApplyBoost(distance);
}
void __wrap_SIM_UpdatePhysics(Vector3_t command, float dt) {
    if (command.x != 0 || command.y != 0 || command.z != 0) {
        assert(locks[0].held && locks[2].held && locks[3].held);
    }
    ++physics;
    last_physics_command = command;
    __real_SIM_UpdatePhysics(command, dt);
}
uint64_t TIME_GetMilliseconds(void) { if (!sample_manual) sample_now += 1000; return sample_now; }
uint32_t TIME_GetSeconds(void) {
    assert(!locks[0].held && !locks[2].held && !locks[3].held && !locks[4].held);
    return 42;
}
void __wrap_FDIR_ReportFault(FaultReport_t report) {
    assert(!locks[0].held && !locks[2].held && !locks[3].held && !locks[4].held);
    ++faults;
    __real_FDIR_ReportFault(report);
}
bool YAMCS_SendEvent(const char *format, ...) {
    assert(!locks[0].held && !locks[2].held && !locks[4].held);
    va_list args;
    va_start(args, format);
    vsnprintf(last_event, sizeof(last_event), format, args);
    va_end(args);
    return true;
}

static void inject_external_mode(void) {
    external_writer = true;
    assert(SYSTEM_MODE_Set(injected_mode));
    external_writer = false;
}
void PayloadSim_GenerateData(uint8_t *buffer, uint16_t length) {
    assert(locks[4].held && !locks[0].held && !locks[2].held && length == 64);
    ++samples;
    memset(buffer, 0xA5, length);
    if (during_generation) { during_generation = false; inject_external_mode(); }
}
ArchiveStatus_t Archive_WriteRecord(uint16_t id, const uint8_t *data, uint16_t length) {
    assert(locks[4].held && !locks[0].held && !locks[2].held);
    assert(id == REC_ID_PAYLOAD && length == 64 && data[0] == 0xA5);
    ++writes;
    if (during_submission) { during_submission = false; inject_external_mode(); }
    return archive_result;
}
void watchdog_pet(WatchdogTaskID_t task) {
    assert((task == WDT_TASK_ADCS && running_task == vADCS_Manager_Task) ||
           (task == WDT_TASK_EPS_MANAGER && running_task == vEPS_Manager_Task));
    for (unsigned i = 0; i < 5; ++i) assert(!locks[i].held);
    ++pets;
}
void vTaskDelay(TickType_t ticks) {
    for (unsigned i = 0; i < 5; ++i) assert(!locks[i].held);
    if (running_task == vDeploymentTask) {
        assert(deployment_waits < 2 && !SYSTEM_MODE_IsDeploymentComplete());
        assert(ticks == pdMS_TO_TICKS(deployment_waits == 0 ? 10000 : 2000));
        ++deployment_waits;
        return; // Model elapsed delay, not a real scheduler or wall-clock wait.
    }
    assert(ticks == pdMS_TO_TICKS(100));
    ++delays;
    longjmp(task_exit, 1);
}
void vTaskDelete(void *task) {
    assert(task == NULL && running_task == vDeploymentTask && deployment_waits == 2);
    for (unsigned i = 0; i < 5; ++i) assert(!locks[i].held);
    assert(SYSTEM_MODE_IsDeploymentComplete());
    ++deployment_deletes;
    longjmp(task_exit, 1);
}
static void run_task(void (*task)(void *)) {
    running_task = task;
    if (setjmp(task_exit) == 0) { task(NULL); assert(!"Task returned unexpectedly"); }
    running_task = NULL;
}
static bool zero(Vector3_t value) { return value.x == 0 && value.y == 0 && value.z == 0; }
static void permission_callback(SystemMode_t mode, void *context) { ++callbacks; }

static void reset_evidence(void) {
    callbacks = samples = writes = faults = boosts = physics = pets = delays = 0;
    logs[0] = last_event[0] = '\0'; log_length = 0;
}
static void seed_eps(unsigned local) {
    if (local == EPS_MODE_NOMINAL) return;
    if (local == EPS_MODE_LOW_VOLTAGE) {
        g_eps_telemetry.f_BusVoltage = 4.99f;
        run_task(vEPS_Manager_Task);
    } else {
        assert(SYSTEM_MODE_Set(MODE_SAFE));
        run_task(vEPS_Manager_Task);
        if (local == EPS_MODE_RECOVERY) {
            assert(SYSTEM_MODE_Set(MODE_NOMINAL));
            run_task(vEPS_Manager_Task);
        }
    }
    assert(EPS_GetMode() == (eps_mode_t)local);
    g_eps_telemetry.f_BusVoltage = 8.5f;
}
static void seed_adcs(ADCSMode_t mode) {
    assert(ADCS_SetMode(mode));
    ADCS_Process(); ADCS_Process();
    assert(!zero(ADCS_GetLastCommand()));
}
static void start_payload(void) {
    assert(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
    assert(Payload_ProcessCommand(PL_CMD_START, 0) == PL_OK);
}
static void assert_adcs_safe(void) {
    assert(zero(ADCS_GetLastCommand()) && ADCS_GetMode() == ADCS_MODE_SAFE);
    ADCSTelemetry_t telemetry;
    ADCS_GetTelemetry(&telemetry);
    assert(!telemetry.actuators_enabled && zero(telemetry.last_torque));
}

static FDIR_ActiveFaultSnapshot_t lifecycle_snapshot(void) {
    FDIR_ActiveFaultSnapshot_t out;
    assert(FDIR_GetActiveFaults(&out));
    return out;
}
static void lifecycle_ground(SystemMode_t mode, bool accepted) {
    const uint8_t command[] = {FDIR_CMD_RESET_MODE, (uint8_t)mode};
    last_event[0] = '\0';
    FDIR_ProcessCommand(command, sizeof(command));
    char expected[64];
    snprintf(expected, sizeof(expected), accepted ? "CMD_ACK: FDIR_SET_MODE=%u" :
             "CMD_NACK: FDIR_SET_MODE rejected=%u", (unsigned)mode);
    assert(strcmp(last_event, expected) == 0);
}
static void lifecycle_ack(void) {
    const uint8_t command[] = {FDIR_CMD_ACK_FAULT};
    FDIR_ProcessCommand(command, sizeof(command));
}
static void lifecycle_no_operations(void) {
    const unsigned before_callbacks = callbacks, before_samples = samples, before_writes = writes;
    for (unsigned operation = 0; operation < 4; ++operation)
        assert(!EPS_RunIfPermitted((EpsOperation_t)operation, permission_callback, NULL));
    assert(callbacks == before_callbacks);
    assert(!ADCS_SetMode(ADCS_MODE_DETUMBLE));
    ADCS_Process(); assert_adcs_safe();
    assert(!ADCS_TryOrbitBoost(50));
    Payload_Update();
    assert(samples == before_samples && writes == before_writes);
}
static uint32_t lifecycle_eps_revision(void) {
    uint32_t revision = 0;
    assert(FDIR_GetFaultRevision(SRC_EPS, FAULT_EPS_LOW_VOLTAGE, &revision));
    return revision;
}
static void lifecycle_low_voltage(void) {
    g_eps_telemetry.f_BusVoltage = 4.99f;
    run_task(vEPS_Manager_Task);
    assert(SYSTEM_MODE_Get() == MODE_SAFE && lifecycle_eps_revision() != 0);
    assert((g_eps_telemetry.ul_PDU_Register & (PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT)) == 0);
    assert(g_eps_telemetry.ul_PDU_Register & PDU_LOAD_COMMS_BIT);
    run_task(vEPS_Manager_Task); // Let the local LOW_VOLTAGE state settle in SAFE.
    assert(EPS_GetMode() == EPS_MODE_SAFE);
    lifecycle_no_operations();
}
static void lifecycle_restore_voltage(void) {
    g_eps_telemetry.f_BusVoltage = 8.5f;
    run_task(vEPS_Manager_Task); // Real EPS owner captures revision, samples and resolves.
    assert(lifecycle_eps_revision() == 0 && EPS_GetMode() == EPS_MODE_SAFE);
    assert((g_eps_telemetry.ul_PDU_Register & (PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT)) == 0);
    lifecycle_no_operations(); // Healthy voltage is not a mode or operation grant.
}
static void lifecycle_resume(SystemMode_t requested, bool hold_payload_power) {
    assert(SYSTEM_MODE_IsDeploymentComplete() && !FDIR_HasActiveFaults());
    assert(SYSTEM_MODE_Get() == MODE_SAFE && EPS_GetMode() == EPS_MODE_SAFE);
    const PayloadState_t prior_payload = Payload_GetTelemetry().current_state;
    const unsigned before_writes = writes;
    lifecycle_ground(requested, true);
    lifecycle_no_operations(); // Local EPS is still SAFE despite the global command.
    run_task(vEPS_Manager_Task);
    assert(EPS_GetMode() == EPS_MODE_RECOVERY);
    lifecycle_no_operations(); // PDU restored, but local RECOVERY still denies work.
    run_task(vEPS_Manager_Task);
    assert(EPS_GetMode() == EPS_MODE_NOMINAL && SYSTEM_MODE_Get() == requested);
    ADCS_Process(); Payload_Update();
    assert_adcs_safe();
    assert(Payload_GetTelemetry().current_state == prior_payload && writes == before_writes);
    assert(!ADCS_SetMode(ADCS_MODE_NOMINAL)); // SAFE requires explicit DETUMBLE first.
    assert(ADCS_SetMode(ADCS_MODE_DETUMBLE));
    ADCS_Process(); assert(zero(ADCS_GetLastCommand())); // No stale B-Dot history replay.
    ADCS_Process(); assert(!zero(ADCS_GetLastCommand()));
    if (hold_payload_power) g_eps_telemetry.ul_PDU_Register &= ~PDU_LOAD_PAYLOAD_BIT;
    assert(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
    assert(Payload_ProcessCommand(PL_CMD_START, 0) == (hold_payload_power ? PL_ERR_INHIBITED : PL_OK));
    Payload_Update();
    assert(writes == before_writes + (hold_payload_power ? 0U : 1U));
    assert(ADCS_SetMode(ADCS_MODE_NOMINAL));
    assert(ADCS_TryOrbitBoost(50) == (requested == MODE_NOMINAL));
}
static void lifecycle(const char *name) {
    static const char *known[] = {
        "lifecycle_boot_nominal", "lifecycle_predeploy_clear_first", "lifecycle_predeploy_complete_first",
        "lifecycle_runtime_recovery", "lifecycle_runtime_degraded", "lifecycle_other_critical",
        "lifecycle_other_fatal", "lifecycle_record_uncertain", "lifecycle_pdu_off",
        "lifecycle_recovery_relapse", "lifecycle_preinit_critical", "lifecycle_preinit_fatal"
    };
    bool recognized = false;
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (strcmp(name, known[i]) == 0) recognized = true;
    assert(recognized);
    assert(!SYSTEM_MODE_IsDeploymentComplete());
    lifecycle_ground(MODE_NOMINAL, false);
    lifecycle_ground(MODE_DEGRADED, false);
    const bool preinit = strncmp(name, "lifecycle_preinit_", 18) == 0;
    if (preinit) {
        const SystemMode_t floor = strstr(name, "fatal") ? MODE_EMERGENCY : MODE_SAFE;
        assert(SYSTEM_MODE_Get() == floor);
        lifecycle_no_operations();
        run_task(vDeploymentTask);
        lifecycle_ack();
        run_task(vEPS_Manager_Task);
        const FDIR_ActiveFaultSnapshot_t out = lifecycle_snapshot();
        assert(out.count == 0 && out.recording_uncertain && FDIR_HasActiveFaults());
        assert(SYSTEM_MODE_Get() == floor);
        lifecycle_ground(MODE_INITIAL_BOOT, false);
        lifecycle_ground(MODE_NOMINAL, false);
        lifecycle_no_operations();
        return;
    }
    assert(SYSTEM_MODE_Get() == MODE_INITIAL_BOOT);
    assert(EPS_RunIfPermitted(EPS_OP_DETUMBLE, permission_callback, NULL));
    assert(!EPS_RunIfPermitted(EPS_OP_POINTING, permission_callback, NULL));
    assert(!EPS_RunIfPermitted(EPS_OP_PAYLOAD, permission_callback, NULL));
    assert(!ADCS_TryOrbitBoost(50));
    if (strcmp(name, "lifecycle_boot_nominal") == 0) {
        run_task(vDeploymentTask);
        assert(SYSTEM_MODE_Get() == MODE_NOMINAL && !FDIR_HasActiveFaults());
        run_task(vEPS_Manager_Task);
        seed_adcs(ADCS_MODE_NOMINAL); start_payload(); Payload_Update();
        assert(writes == 1 && Payload_GetTelemetry().bytes_generated == 64);
        return;
    }

    const bool predeploy = strncmp(name, "lifecycle_predeploy_", 20) == 0;
    if (!predeploy) {
        run_task(vDeploymentTask);
        seed_adcs(ADCS_MODE_NOMINAL); start_payload(); Payload_Update();
        assert(writes == 1);
    }
    lifecycle_low_voltage();
    const uint32_t first_revision = lifecycle_eps_revision();
    lifecycle_ack(); // Neither warning ACK nor completed deployment clears the EPS cause.
    assert(lifecycle_eps_revision() == first_revision);
    lifecycle_ground(MODE_NOMINAL, false);
    if (strcmp(name, "lifecycle_predeploy_complete_first") == 0) {
        run_task(vDeploymentTask);
        assert(SYSTEM_MODE_Get() == MODE_SAFE && lifecycle_eps_revision() == first_revision);
    }

    const bool other_critical = strcmp(name, "lifecycle_other_critical") == 0;
    const bool other_fatal = strcmp(name, "lifecycle_other_fatal") == 0;
    const bool uncertain = strcmp(name, "lifecycle_record_uncertain") == 0;
    uint32_t other_revision = 0;
    if (other_critical || other_fatal || uncertain) {
        if (uncertain) fail_lock = &locks[0];
        FDIR_ReportFault((FaultReport_t){.source = SRC_COMMS, .fault_code = 0x7F01,
            .severity = other_fatal ? FAULT_FATAL : FAULT_CRITICAL});
        assert(FDIR_GetFaultRevision(SRC_COMMS, 0x7F01, &other_revision));
        assert((other_revision != 0) == !uncertain);
    }
    lifecycle_restore_voltage();
    if (predeploy && deployment_deletes == 0) {
        lifecycle_ground(MODE_NOMINAL, false); // No cause now, but deployment is incomplete.
        run_task(vDeploymentTask);
        assert(SYSTEM_MODE_Get() == MODE_SAFE);
    }
    assert(SYSTEM_MODE_IsDeploymentComplete());
    if (other_critical || other_fatal || uncertain) {
        lifecycle_ack();
        assert(SYSTEM_MODE_CompleteDeployment());
        const FDIR_ActiveFaultSnapshot_t out = lifecycle_snapshot();
        assert(out.count == (uncertain ? 0U : 1U) && out.recording_uncertain == uncertain);
        if (!uncertain) {
            assert(out.records[0].source == SRC_COMMS && out.records[0].code == 0x7F01);
            assert(out.records[0].revision == other_revision && out.records[0].fatal == other_fatal);
        }
        assert(SYSTEM_MODE_Get() == (other_fatal ? MODE_EMERGENCY : MODE_SAFE));
        assert(FDIR_HasActiveFaults());
        lifecycle_ground(MODE_INITIAL_BOOT, false);
        lifecycle_ground(MODE_NOMINAL, false);
        lifecycle_ground(MODE_DEGRADED, false);
        assert(!SYSTEM_MODE_Init(MODE_INITIAL_BOOT) && !FDIR_Init());
        const uint32_t generated_before_reset = Payload_GetTelemetry().bytes_generated;
        assert(Payload_ProcessCommand(PL_CMD_RESET, 0) == PL_OK);
        assert(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
        assert(Payload_ProcessCommand(PL_CMD_START, 0) == PL_ERR_INHIBITED);
        lifecycle_no_operations();
        assert(Payload_GetTelemetry().bytes_generated == generated_before_reset);
        const FDIR_ActiveFaultSnapshot_t retained = lifecycle_snapshot();
        assert(retained.count == out.count && retained.recording_uncertain == out.recording_uncertain);
        if (!uncertain) {
            assert(retained.records[0].source == SRC_COMMS && retained.records[0].code == 0x7F01);
            assert(retained.records[0].revision == other_revision && retained.records[0].fatal == other_fatal);
        }
        assert(FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == (other_fatal ? MODE_EMERGENCY : MODE_SAFE));
        lifecycle_ground(MODE_NOMINAL, false);
        return;
    }
    assert(!FDIR_HasActiveFaults() && SYSTEM_MODE_Get() == MODE_SAFE);
    if (strcmp(name, "lifecycle_recovery_relapse") == 0) {
        lifecycle_ground(MODE_NOMINAL, true);
        run_task(vEPS_Manager_Task);
        assert(EPS_GetMode() == EPS_MODE_RECOVERY);
        lifecycle_low_voltage();
        assert(lifecycle_eps_revision() > first_revision);
        lifecycle_ground(MODE_NOMINAL, false);
        lifecycle_restore_voltage();
    }
    lifecycle_resume(strcmp(name, "lifecycle_runtime_degraded") == 0 ? MODE_DEGRADED : MODE_NOMINAL,
                     strcmp(name, "lifecycle_pdu_off") == 0);
}

static bool matrix(const char *name) {
    static const char *globals[] = {"boot", "safe", "nominal", "degraded", "emergency", "critical"};
    static const char *locals[] = {"nominal", "low", "safe", "recovery"};
    static const char *operations[] = {"detumble", "pointing", "boost", "payload"};
    char candidate[80];
    for (unsigned g = 0; g < 6; ++g) {
        for (unsigned l = 0; l < 4; ++l) {
            for (unsigned op = 0; op < 4; ++op) {
                snprintf(candidate, sizeof(candidate), "permit_%s_%s_%s", globals[g], locals[l], operations[op]);
                if (strcmp(name, candidate) != 0) continue;
                seed_eps(l);
                bool selectable = l != EPS_MODE_LOW_VOLTAGE ||
                    (g != MODE_INITIAL_BOOT && g != MODE_NOMINAL && g != MODE_DEGRADED);
                assert(SYSTEM_MODE_Set((SystemMode_t)g) == selectable);
                SystemMode_t effective = selectable ? (SystemMode_t)g : MODE_SAFE;
                if (!selectable) assert(FDIR_HasActiveFaults());
                reset_evidence();
                bool mode_allowed = g == MODE_NOMINAL || g == MODE_DEGRADED;
                if (op == EPS_OP_DETUMBLE) mode_allowed |= g == MODE_INITIAL_BOOT;
                if (op == EPS_OP_ORBIT_BOOST) mode_allowed = g == MODE_NOMINAL;
                bool allowed = mode_allowed && l == EPS_MODE_NOMINAL;
                assert(EPS_RunIfPermitted((EpsOperation_t)op, permission_callback, NULL) == allowed);
                assert(callbacks == (unsigned)allowed && SYSTEM_MODE_Get() == effective);
                return true;
            }
        }
        for (unsigned local = 0; local < 2; ++local) {
            snprintf(candidate, sizeof(candidate), "adcs_%s_%s", globals[g], local ? "pointing" : "detumble");
            if (strcmp(name, candidate) != 0) continue;
            seed_adcs(local ? ADCS_MODE_NOMINAL : ADCS_MODE_DETUMBLE);
            assert(SYSTEM_MODE_Set((SystemMode_t)g)); reset_evidence(); ADCS_Process();
            bool allowed = g == MODE_NOMINAL || g == MODE_DEGRADED || (g == MODE_INITIAL_BOOT && !local);
            if (!allowed) assert_adcs_safe();
            else assert(!zero(ADCS_GetLastCommand()));
            ADCS_StepSimulation(0.1f);
            assert(physics == 1 && zero(last_physics_command) == !allowed);
            return true;
        }
        for (unsigned update = 0; update < 2; ++update) {
            snprintf(candidate, sizeof(candidate), "payload_%s_%s", globals[g], update ? "update" : "start");
            if (strcmp(name, candidate) != 0) continue;
            if (update) start_payload();
            else assert(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
            assert(SYSTEM_MODE_Set((SystemMode_t)g)); reset_evidence();
            bool allowed = g == MODE_NOMINAL || g == MODE_DEGRADED;
            if (update) {
                Payload_Update();
                assert(samples == (unsigned)allowed && writes == (unsigned)allowed);
                assert(Payload_GetTelemetry().current_state == (allowed ? PL_STATE_ACTIVE : PL_STATE_SAFE));
            } else {
                assert(Payload_ProcessCommand(PL_CMD_START, 0) == (allowed ? PL_OK : PL_ERR_INHIBITED));
                assert(samples == 0 && writes == 0 && faults == 0);
            }
            return true;
        }
        snprintf(candidate, sizeof(candidate), "boost_%s", globals[g]);
        if (strcmp(name, candidate) == 0) {
            seed_adcs(ADCS_MODE_NOMINAL);
            assert(SYSTEM_MODE_Set((SystemMode_t)g)); reset_evidence();
            assert(ADCS_TryOrbitBoost(50) == (g == MODE_NOMINAL));
            assert(boosts == (unsigned)(g == MODE_NOMINAL));
            return true;
        }
    }
    return false;
}

static void special(const char *name) {
    if (strcmp(name, "permission_invalid") == 0) {
        assert(!EPS_RunIfPermitted((EpsOperation_t)-1, permission_callback, NULL));
        assert(!EPS_RunIfPermitted((EpsOperation_t)4, permission_callback, NULL));
        assert(!EPS_RunIfPermitted(EPS_OP_PAYLOAD, NULL, NULL) && callbacks == 0);
    } else if (strcmp(name, "permission_pdu") == 0 || strcmp(name, "permission_voltage") == 0 ||
               strcmp(name, "permission_flag") == 0 || strcmp(name, "permission_nan") == 0 ||
               strcmp(name, "permission_infinity") == 0) {
        if (strcmp(name, "permission_pdu") == 0) g_eps_telemetry.ul_PDU_Register &= ~PDU_LOAD_PAYLOAD_BIT;
        if (strcmp(name, "permission_voltage") == 0) g_eps_telemetry.f_BusVoltage = 4.99f;
        if (strcmp(name, "permission_flag") == 0) g_eps_telemetry.xFaultFlags.safe_mode_active = 1;
        if (strcmp(name, "permission_nan") == 0) g_eps_telemetry.f_BusVoltage = NAN;
        if (strcmp(name, "permission_infinity") == 0) g_eps_telemetry.f_BusVoltage = INFINITY;
        assert(!EPS_RunIfPermitted(EPS_OP_PAYLOAD, permission_callback, NULL) && callbacks == 0);
    } else if (strcmp(name, "permission_uninitialized") == 0) {
        assert(!EPS_RunIfPermitted(EPS_OP_PAYLOAD, permission_callback, NULL) && callbacks == 0);
    } else if (strcmp(name, "permission_eps_lock") == 0 || strcmp(name, "permission_mode_lock") == 0 ||
               strcmp(name, "permission_null_mutex") == 0) {
        if (strcmp(name, "permission_null_mutex") == 0) xEPSDataMutex = NULL;
        else fail_lock = &locks[strcmp(name, "permission_eps_lock") == 0 ? 2 : 0];
        assert(!EPS_RunIfPermitted(EPS_OP_PAYLOAD, permission_callback, NULL) && callbacks == 0);
        xEPSDataMutex = &locks[2];
    } else if (strcmp(name, "adcs_stale_torque") == 0 || strcmp(name, "adcs_stale_dipole") == 0) {
        seed_adcs(strcmp(name, "adcs_stale_torque") == 0 ? ADCS_MODE_NOMINAL : ADCS_MODE_DETUMBLE);
        assert(SYSTEM_MODE_Set(MODE_SAFE)); assert_adcs_safe();
        assert(SYSTEM_MODE_Set(MODE_NOMINAL)); ADCS_Process(); assert_adcs_safe();
        assert(!ADCS_SetMode(ADCS_MODE_NOMINAL));
        assert(ADCS_SetMode(ADCS_MODE_DETUMBLE)); ADCS_Process();
        assert(zero(ADCS_GetLastCommand())); // Re-prime history, do not replay the old derivative.
        ADCS_Process(); assert(!zero(ADCS_GetLastCommand()));
    } else if (strcmp(name, "adcs_publish_race") == 0 || strcmp(name, "adcs_physics_race") == 0 ||
               strcmp(name, "adcs_boost_race") == 0) {
        seed_adcs(ADCS_MODE_NOMINAL); reset_evidence();
        guard_inject_countdown = strcmp(name, "adcs_publish_race") == 0 ? 2 : 1;
        if (strcmp(name, "adcs_publish_race") == 0) { ADCS_Process(); assert_adcs_safe(); }
        else if (strcmp(name, "adcs_physics_race") == 0) {
            ADCS_StepSimulation(0.1f); assert(physics == 1 && zero(last_physics_command)); assert_adcs_safe();
        } else { assert(!ADCS_TryOrbitBoost(50) && boosts == 0); }
        assert(SYSTEM_MODE_Get() == MODE_EMERGENCY && guard_inject_countdown == 0);
    } else if (strcmp(name, "adcs_invalid_modes") == 0) {
        seed_adcs(ADCS_MODE_NOMINAL);
        assert(!ADCS_SetMode((ADCSMode_t)-1) && !ADCS_SetMode((ADCSMode_t)5));
        assert(!ADCS_SetMode(ADCS_MODE_SUN_SAFE) && !ADCS_SetMode(ADCS_MODE_PAYLOAD_POINT));
        assert(ADCS_GetMode() == ADCS_MODE_NOMINAL);
        const uint8_t missing[] = {ADCS_CMD_SET_MODE};
        ADCS_ProcessCommand(NULL, 2); ADCS_ProcessCommand(missing, sizeof(missing));
    } else if (strcmp(name, "adcs_saturation") == 0 || strcmp(name, "adcs_nan") == 0) {
        gyro.x = strcmp(name, "adcs_nan") == 0 ? NAN : 3001;
        ADCS_Process(); assert_adcs_safe(); assert(faults == 1);
    } else if (strncmp(name, "adcs_rate_", 10) == 0 || strcmp(name, "adcs_nominal_saturation") == 0) {
        const bool above = strcmp(name, "adcs_rate_above") == 0 ||
            strcmp(name, "adcs_rate_negative") == 0 || strcmp(name, "adcs_nominal_saturation") == 0;
        if (strcmp(name, "adcs_nominal_saturation") == 0) assert(ADCS_SetMode(ADCS_MODE_NOMINAL));
        gyro = (Vector3_t){ADCS_MAX_RATE_RAD_S, 0, 0};
        if (strcmp(name, "adcs_rate_below") == 0) gyro.x -= 0.001f;
        if (above) gyro.x += 0.001f;
        if (strcmp(name, "adcs_rate_negative") == 0) gyro.x = -gyro.x;
        ADCS_Process();
        assert(faults == (unsigned)above);
        if (above) assert_adcs_safe();
        else assert(ADCS_GetMode() == ADCS_MODE_DETUMBLE);
    } else if (strcmp(name, "adcs_nominal_damping") == 0) {
        SIM_SetTrueAngularVelocity(1, 0, 0);
        seed_adcs(ADCS_MODE_NOMINAL);
        assert(ADCS_GetLastCommand().x < 0);
        ADCS_StepSimulation(ADCS_CYCLE_S);
        assert(last_physics_command.x > 0 && fabsf(__real_SIM_ReadGyroscope().x) < 0.011f);
        assert(faults == 0);
    } else if (strcmp(name, "adcs_invalid_dt") == 0) {
        seed_adcs(ADCS_MODE_NOMINAL); reset_evidence();
        const float invalid[] = {0, -0.1f, NAN, INFINITY};
        for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) ADCS_StepSimulation(invalid[i]);
        assert(physics == 0 && faults == 0 && ADCS_GetMode() == ADCS_MODE_NOMINAL);
    } else if (strcmp(name, "adcs_stable_boot") == 0) {
        gyro = (Vector3_t){0, 0, 0}; assert(SYSTEM_MODE_Set(MODE_INITIAL_BOOT));
        ADCS_Process(); assert(ADCS_GetMode() == ADCS_MODE_DETUMBLE);
        assert(SYSTEM_MODE_Set(MODE_NOMINAL)); ADCS_Process(); assert(ADCS_GetMode() == ADCS_MODE_NOMINAL);
    } else if (strcmp(name, "adcs_state_lock") == 0) {
        seed_adcs(ADCS_MODE_NOMINAL); fail_lock = &locks[3]; ADCS_StepSimulation(0.1f);
        assert(physics == 0); // Never advance the shared plant without owning ADCS state.
        assert(zero(last_physics_command)); fail_lock = &locks[3]; assert(zero(ADCS_GetLastCommand()));
        fail_lock = &locks[3]; assert(!ADCS_SetMode(ADCS_MODE_SAFE));
        fail_lock = &locks[3]; assert(isnan(ADCS_GetAltitude()));
        fail_lock = &locks[3]; assert(ADCS_GetFuelLevel() == 0);
    } else if (strcmp(name, "manager_safe") == 0 || strcmp(name, "manager_boost") == 0 ||
               strcmp(name, "manager_insufficient_fuel") == 0) {
        seed_adcs(ADCS_MODE_NOMINAL);
        if (strcmp(name, "manager_safe") == 0) assert(SYSTEM_MODE_Set(MODE_SAFE));
        if (strcmp(name, "manager_insufficient_fuel") == 0) {
            while (__real_SIM_ApplyBoost(50)) { }
            assert(SIM_GetFuelLevel() == 2.5f); // Not enough for the next 7.5% maneuver.
        }
        reset_evidence(); run_task(vADCS_Manager_Task);
        bool success = strcmp(name, "manager_boost") == 0;
        assert(pets == 1 && delays == 1 && physics == 1);
        assert((strstr(logs, "Maintenance Complete") != NULL) == success);
        if (strcmp(name, "manager_safe") == 0) assert(boosts == 0 && zero(last_physics_command));
    } else if (strcmp(name, "payload_recovery") == 0) {
        start_payload(); Payload_Update(); assert(Payload_GetTelemetry().bytes_generated == 64);
        assert(SYSTEM_MODE_Set(MODE_SAFE)); Payload_Update();
        assert(Payload_GetTelemetry().current_state == PL_STATE_SAFE && writes == 1);
        assert(SYSTEM_MODE_Set(MODE_NOMINAL)); Payload_Update(); assert(writes == 1);
        assert(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
        assert(Payload_ProcessCommand(PL_CMD_START, 0) == PL_OK); Payload_Update(); assert(writes == 2);
    } else if (strcmp(name, "payload_prepare_race") == 0 || strcmp(name, "payload_start_race") == 0 ||
               strcmp(name, "payload_commit_race") == 0) {
        if (strcmp(name, "payload_start_race") == 0) {
            assert(Payload_ProcessCommand(PL_CMD_INIT, 0) == PL_OK);
            guard_inject_countdown = 1;
            assert(Payload_ProcessCommand(PL_CMD_START, 0) == PL_ERR_INHIBITED && writes == 0);
        } else {
            start_payload();
            if (strcmp(name, "payload_prepare_race") == 0) during_generation = true;
            else post_guard_inject_countdown = 2; // Fault after final permission, before Archive_WriteRecord.
            Payload_Update();
            assert(samples == 1 && writes == (unsigned)(strcmp(name, "payload_commit_race") == 0));
            Payload_Update(); assert(Payload_GetTelemetry().current_state == PL_STATE_SAFE && samples == 1);
            if (strcmp(name, "payload_commit_race") == 0) {
                assert(SYSTEM_MODE_Set(MODE_NOMINAL));
                start_payload(); during_submission = true;
                Payload_Update(); // Also check a fault inside the already-submitted archive transaction.
                assert(samples == 2 && writes == 2);
                Payload_Update(); assert(Payload_GetTelemetry().current_state == PL_STATE_SAFE && samples == 2);
            }
        }
        assert(SYSTEM_MODE_Get() == MODE_EMERGENCY);
    } else if (strcmp(name, "payload_power_cut") == 0) {
        start_payload(); g_eps_telemetry.ul_PDU_Register &= ~PDU_LOAD_PAYLOAD_BIT;
        Payload_Update(); assert(samples == 0 && writes == 0 && Payload_GetTelemetry().current_state == PL_STATE_SAFE);
    } else if (strcmp(name, "payload_state_lock") == 0) {
        start_payload(); fail_lock = &locks[4]; Payload_Update(); assert(samples == 0 && writes == 0);
        fail_lock = &locks[4]; assert(Payload_ProcessCommand(PL_CMD_START, 0) == PL_ERR_UNAVAILABLE);
        fail_lock = &locks[4]; assert(Payload_GetTelemetry().current_state == PL_STATE_SAFE);
    } else if (strcmp(name, "payload_archive_failure") == 0) {
        start_payload(); archive_result = ARCHIVE_ERR_FULL; Payload_Update();
        PayloadTelemetry_t telemetry = Payload_GetTelemetry();
        assert(telemetry.current_state == PL_STATE_ERROR && telemetry.bytes_generated == 0 && telemetry.error_counter == 1);
        assert(faults == 1 && writes == 1); Payload_Update(); assert(writes == 1);
    } else if (strcmp(name, "payload_ack_nack") == 0) {
        const uint8_t init[] = {PL_CMD_INIT}, start[] = {PL_CMD_START};
        PAYLOAD_ProcessCommandWrapper(init, sizeof(init)); assert(strstr(last_event, "CMD_ACK") != NULL);
        assert(SYSTEM_MODE_Set(MODE_SAFE)); PAYLOAD_ProcessCommandWrapper(start, sizeof(start));
        assert(strstr(last_event, "CMD_NACK") != NULL && faults == 0);
        PAYLOAD_ProcessCommandWrapper(NULL, 2); assert(strstr(last_event, "CMD_NACK") != NULL);
    } else if (strcmp(name, "payload_stop_reset") == 0) {
        start_payload(); assert(SYSTEM_MODE_Set(MODE_CRITICAL));
        assert(Payload_ProcessCommand(PL_CMD_STOP, 0) == PL_OK);
        assert(Payload_ProcessCommand(PL_CMD_RESET, 0) == PL_OK);
        Payload_Update(); assert(Payload_GetTelemetry().current_state == PL_STATE_OFF && samples == 0);
    } else if (strcmp(name, "payload_rate_invalid") == 0) {
        assert(Payload_ProcessCommand(PL_CMD_SET_RATE, 0) == PL_ERR_INVALID_STATE && faults == 1);
        assert(Payload_ProcessCommand(PL_CMD_SET_RATE, 11) == PL_ERR_INVALID_STATE && faults == 2);
        assert(Payload_ProcessCommand(PL_CMD_SET_RATE, 10) == PL_OK);
        sample_manual = true; sample_now = 100;
        start_payload(); Payload_Update(); assert(writes == 1);
        Payload_Update(); assert(writes == 1); // No burst on repeated updates.
        sample_now = 199; Payload_Update(); assert(writes == 1);
        sample_now = 200; Payload_Update(); assert(writes == 2);
        assert(Payload_ProcessCommand(PL_CMD_SET_RATE, 1) == PL_OK);
        Payload_Update(); assert(writes == 3);
        sample_now = 1199; Payload_Update(); assert(writes == 3);
        sample_now = 1200; Payload_Update(); assert(writes == 4);
        sample_now = 9000; Payload_Update(); assert(writes == 5); // No catch-up loop.
        sample_now = 0; Payload_Update(); assert(writes == 5); // Regressed clock cannot burst.
        assert(SYSTEM_MODE_Set(MODE_SAFE)); Payload_Update();
        assert(Payload_GetTelemetry().current_state == PL_STATE_SAFE && writes == 5);
    } else if (strcmp(name, "sim_boost_parameters") == 0) {
        assert(!__real_SIM_ApplyBoost(0) && !__real_SIM_ApplyBoost(-1) && !__real_SIM_ApplyBoost(NAN));
        assert(__real_SIM_ApplyBoost(50));
    } else {
        assert(!"unknown special scenario");
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const bool lifecycle_case = strncmp(argv[1], "lifecycle_", 10) == 0;
    const bool preinit_critical = strcmp(argv[1], "lifecycle_preinit_critical") == 0;
    const bool preinit_fatal = strcmp(argv[1], "lifecycle_preinit_fatal") == 0;
    allocating = 0;
    if (preinit_critical || preinit_fatal) {
        FDIR_ReportFault((FaultReport_t){.source = SRC_EPS, .fault_code = FAULT_EPS_LOW_VOLTAGE,
            .severity = preinit_fatal ? FAULT_FATAL : FAULT_CRITICAL});
    }
    if (strcmp(argv[1], "permission_uninitialized") != 0) {
        // Component permission tests start from a completed deployment, not a bypass init.
        assert(SYSTEM_MODE_Init(MODE_INITIAL_BOOT));
        if (preinit_critical || preinit_fatal) {
            assert(strstr(logs, preinit_fatal ? "SYSTEM MODE: Initialized in EMERGENCY." :
                          "SYSTEM MODE: Initialized in SAFE.") != NULL);
            assert(strstr(logs, "SYSTEM MODE: Initialized in INITIAL_BOOT.") == NULL);
        }
        if (!lifecycle_case) assert(SYSTEM_MODE_CompleteDeployment());
    }
    allocating = 1; assert(FDIR_Init());
    g_eps_telemetry.f_BusVoltage = 8.5f;
    g_eps_telemetry.ul_PDU_Register = PDU_LOAD_PAYLOAD_BIT | PDU_LOAD_HEATER_BIT | PDU_LOAD_COMMS_BIT;
    bool init_failure = strstr(argv[1], "init_") != NULL && strstr(argv[1], "failure") != NULL;
    if (init_failure) {
        bool adcs = strncmp(argv[1], "adcs", 4) == 0;
        allocating = adcs ? 3 : 4; abort_expected = true;
        if (strstr(argv[1], "lock_failure") != NULL) fail_lock = &locks[allocating];
        else fail_allocation = true;
        if (setjmp(abort_exit) == 0) { if (adcs) ADCS_Init(); else Payload_Init(); assert(!"missing abort"); }
        assert(strstr(logs, "Required state mutex initialization failed") != NULL);
    } else {
        allocating = 3; ADCS_Init(); allocating = 4; Payload_Init(); reset_evidence();
        if (lifecycle_case) lifecycle(argv[1]);
        else if (!matrix(argv[1])) special(argv[1]);
    }
    if (lifecycle_case) assert(deployment_waits == 2 && deployment_deletes == 1);
    for (unsigned i = 0; i < 5; ++i) assert(!locks[i].held);
    fprintf(stdout, "PASS: subsystem safety %s.\n", argv[1]);
    return 0;
}
