function Get-HostTestPlan {
    param([Parameter(Mandatory)][string]$ProjectRoot)

    # Enumerate every exported top-level header. Each gets an isolated translation
    # unit, its own component path, and only explicit public include dependencies.
    $publicHeaderIncludes = @{
        comms = @('components/fsw_common')
        eps = @('components/system_mode', 'tests/host/startup_core/include')
        fdir = @('components/system_mode')
        watchdog = @('components/fsw_common')
        main = @('tests/host/startup_core/include')
    }
    $headerChecks = @(
        $headerDirectories = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'components') -Directory) +
                             @(Get-Item -LiteralPath (Join-Path $projectRoot 'main'))
        foreach ($directory in $headerDirectories) {
            $relativeDirectory = if ($directory.Name -eq 'main') { 'main' } else { 'components/' + $directory.Name }
            foreach ($header in Get-ChildItem -LiteralPath $directory.FullName -Filter '*.h' -File | Sort-Object Name) {
                $includes = @($relativeDirectory)
                if ($publicHeaderIncludes.ContainsKey($directory.Name)) { $includes += $publicHeaderIncludes[$directory.Name] }
                @{ Name = $relativeDirectory + '/' + $header.Name; Includes = $includes }
            }
        }
    )

    $hostSuiteTests = @(
        @{
            Name = 'eps_sim_check'
            Includes = @('tests/host/eps_mode/include', 'components/eps', 'components/adcs', 'main')
            Sources = @('tests/host/eps_sim/check.c', 'main/eps_harvest.c')
            LinkFlags = @('-lm', '-Wl,--wrap=printf', '-fno-builtin-printf')
        },
        @{
            Name = 'recovery_loop_injection_check'
            Includes = @('tests/host/recovery/include', 'main', 'components/recovery')
            Sources = @('tests/host/recovery/loop_injection_check.c', 'main/startup_fault_injection.c')
            LinkFlags = @('-DFSW_TEST_STARTUP_FAILURE=1', '-DFSW_TEST_RECOVERY_BOOT_LOOP=1', '-fno-builtin-printf', '-Wl,--wrap=printf')
            Cases = @('failed_0','failed_1','failed_2','failed_3','failed_4','snapshot','storage','stable',
                      'zero_boot','no_open','fatal','early_loop','wrong_first_reason','wrong_later_reason','reason_mismatch','real_failure')
        },
        @{
            Name = 'recovery_app_check'
            Includes = @('tests/host/recovery/include', 'tests/host/startup_nvs/include', 'main', 'components/recovery',
                         'components/system_mode', 'components/eps', 'components/fdir', 'components/watchdog',
                         'components/archive', 'components/payload', 'components/time', 'components/fsw_common')
            Sources = @('tests/host/recovery/app_check.c', 'main/recovery_app.c')
            LinkFlags = @('-fno-builtin-printf', '-Wl,--wrap=printf')
            Cases = @('healthy', 'rearmed', 'storage_failed', 'nvs_failure', 'create_failure', 'repeat_start', 'archive_crc',
                      'archive_busy', 'snapshot_failure', 'critical', 'fatal', 'uncertain', 'overflow', 'undeployed',
                      'emergency', 'critical_mode', 'eps_lock', 'eps_give_failure', 'voltage_low', 'voltage_nan', 'voltage_inf', 'payload_error',
                      'timing_failure', 'no_pet', 'stale_0', 'stale_1', 'stale_2', 'stale_3', 'stale_4', 'stale_5', 'stale_6')
        },
        @{
            Name = 'recovery_port_check'
            Includes = @('tests/host/recovery/include', 'tests/host/eps_mode/include', 'components/recovery')
            Sources = @('tests/host/recovery/port_check.c', 'components/recovery/recovery_port_esp32.c')
            LinkFlags = @()
            Cases = @('allocation', 'lock_busy', 'load_ok', 'namespace_missing', 'key_missing', 'open_error',
                      'get_error', 'wrong_size', 'save_ok', 'set_error', 'commit_error', 'readback_mismatch')
        },
        @{
            Name = 'recovery_policy_check'
            Includes = @('tests/host/eps_mode/include', 'components/recovery', 'components/system_mode', 'components/fsw_common')
            Sources = @('tests/host/recovery/check.c', 'components/recovery/recovery.c', 'components/system_mode/system_mode.c')
            LinkFlags = @()
            Cases = @('fresh', 'software', 'reason_2', 'reason_4', 'reason_5', 'reason_6', 'reason_7', 'reason_9', 'reason_0',
                      'prior_healthy', 'history', 'approved_history', 'unfinished', 'failed_boots', 'corrupt', 'invalid_flags',
                      'invalid_version', 'invalid_reserved', 'invalid_boot', 'counter_exhausted', 'load_error', 'no_storage',
                      'lock_init_failure', 'init_write_error', 'repeat_init', 'disable_busy', 'stable_write_error', 'write_bound',
                      'health_gap', 'history_bound', 'fault_write_error', 'rollover', 'bad_key', 'stale_token', 'stale_health',
                      'health_loss', 'release_critical', 'release_fatal', 'release_race', 'undeployed', 'rearm_write_error', 'rearm',
                      'command_null', 'command_short', 'command_long', 'command_bad_key', 'command_good')
        },
        @{
            Name = 'mission_lifecycle_injection_check'
            Includes = @('tests/host/mission_lifecycle/include', 'tests/host/eps_mode/include', 'main', 'components/eps', 'components/system_mode',
                         'components/fdir', 'components/adcs', 'components/payload', 'components/archive', 'components/fsw_common')
            Sources = @('tests/host/mission_lifecycle/check.c', 'main/mission_lifecycle_test.c')
            LinkFlags = @('-DFSW_TEST_MISSION_LIFECYCLE=1', '-fno-builtin-printf', '-Wl,--wrap=printf')
            Cases = @('success', 'demo_disabled', 'bad_argument', 'eps_lock', 'snapshot_failure', 'overflow', 'uncertain',
                      'usage_unavailable', 'unexpected_command', 'boot_accept', 'boot_payload_allowed', 'missing_ground',
                      'no_deployment', 'no_data', 'comms_shed', 'adcs_not_inhibited', 'safe_data_leak', 'no_owner_clear',
                      'automatic_resume', 'implicit_rearm', 'direct_rearm', 'other_cause', 'fatal', 'retention_corruption', 'stack_low')
        },
        @{
            Name = 'startup_injection_hk_check'
            Includes = @('tests/host/startup_injection/include', 'main')
            Sources = @('tests/host/startup_injection/check.c', 'main/startup_fault_injection.c')
            LinkFlags = @('-DFSW_TEST_STARTUP_FAILURE=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=fflush', '-Wl,--wrap=abort')
            Cases = @('armed', 'panic_suppressed', 'wrong_reset_armed', 'repeat_init', 'real_failure_preserved')
        },
        @{
            Name = 'startup_injection_early_check'
            Includes = @('tests/host/startup_injection/include', 'main')
            Sources = @('tests/host/startup_injection/check.c', 'main/startup_fault_injection.c')
            LinkFlags = @('-DFSW_TEST_STARTUP_FAILURE=2', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=fflush', '-Wl,--wrap=abort')
            Cases = @('armed', 'panic_suppressed', 'wrong_reset_armed', 'repeat_init', 'real_failure_preserved',
                      'before_init', 'other_passthrough', 'repeat_target', 'bad_name', 'bad_null_name',
                      'bad_stack', 'bad_argument', 'bad_priority', 'bad_handle', 'bad_core')
        },
        @{
            Name = 'startup_injection_late_check'
            Includes = @('tests/host/startup_injection/include', 'main')
            Sources = @('tests/host/startup_injection/check.c', 'main/startup_fault_injection.c')
            LinkFlags = @('-DFSW_TEST_STARTUP_FAILURE=3', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=fflush', '-Wl,--wrap=abort')
            Cases = @('armed', 'panic_suppressed', 'wrong_reset_armed', 'repeat_init', 'real_failure_preserved',
                      'before_init', 'other_passthrough', 'repeat_target', 'bad_name', 'bad_null_name',
                      'bad_stack', 'bad_argument', 'bad_priority', 'bad_handle', 'bad_core')
        },
        @{
            Name = 'resource_monitor_check'
            Includes = @('tests/host/resource_monitor/include', 'main', 'components/watchdog',
                         'components/fsw_common', 'components/time')
            Sources = @('tests/host/resource_monitor/check.c', 'main/resource_monitor.c')
            LinkFlags = @('-fno-builtin-fprintf', '-Wl,--wrap=fprintf')
            Cases = @('normal', 'missing', 'timing_unavailable', 'allocation_failure', 'bad_queues', 'repeat_start', 'tick_wrap')
        },
        @{
            Name = 'archive_wire_check'
            Includes = @('tests/host/eps_mode/include', 'tests/host/archive_sync', 'components/archive',
                         'components/fsw_common', 'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/archive_wire/check.c', 'tests/host/archive_sync/mutex_mock.c',
                        'components/archive/archive_service.c', 'components/archive/utils.c')
            LinkFlags = @('-pthread', '-D_POSIX_C_SOURCE=200809L', '-DFSW_TEST_ARCHIVE_CORRUPTION=1',
                          '-fno-builtin-printf', '-Wl,--wrap=printf')
        },
        @{
            Name = 'archive_transfer_check'
            Includes = @('tests/host/eps_mode/include', 'tests/host/archive_sync', 'components/archive',
                         'components/fsw_common', 'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/archive_transfer/check.c', 'tests/host/archive_sync/mutex_mock.c',
                        'components/archive/archive_service.c', 'components/archive/utils.c')
            LinkFlags = @('-pthread', '-D_POSIX_C_SOURCE=200809L', '-DFSW_TEST_ARCHIVE_CORRUPTION=1',
                          '-fno-builtin-printf', '-Wl,--wrap=printf')
        },
        @{
            Name = 'archive_port_check'
            Includes = @('tests/host/archive_port/include', 'components/archive')
            Sources = @('tests/host/archive_port/check.c', 'components/archive/archive_port_esp32.c')
            LinkFlags = @()
        },
        @{
            Name = 'archive_sync_check'
            Includes = @('tests/host/eps_mode/include', 'components/archive', 'components/fsw_common',
                         'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/archive_sync/check.c', 'tests/host/archive_sync/mutex_mock.c',
                        'components/archive/archive_service.c', 'components/archive/utils.c')
            LinkFlags = @('-pthread', '-D_POSIX_C_SOURCE=200809L', '-DFSW_TEST_ARCHIVE_CORRUPTION=1',
                          '-fno-builtin-printf', '-Wl,--wrap=printf')
        },
        @{
            Name = 'data_logger_check'
            Includes = @('tests/host/tm_gen/include', 'components/cdh', 'components/archive',
                         'components/payload', 'components/system_mode')
            Sources = @('tests/host/data_logger/check.c', 'components/cdh/data_logger.c')
            LinkFlags = @('-fno-builtin-printf', '-Wl,--wrap=printf')
        },
        @{
            Name = 'tc_ingress_check'
            Includes = @('tests/host/network_startup/include', 'tests/host/tc_frame/include',
                         'components/cdh', 'components/comms', 'components/time', 'components/adcs',
                         'components/eps', 'components/hk', 'components/archive', 'components/fdir',
                         'components/payload', 'components/system_mode', 'components/recovery')
            Sources = @('tests/host/tc_ingress/check.c', 'components/cdh/tc_udp_rx.c',
                        'components/cdh/tc_proc.c', 'components/cdh/cdhs_router.c',
                        'components/cdh/utils.c', 'components/comms/ccsds_packet.c')
            LinkFlags = @('-D_POSIX_C_SOURCE=200809L')
        },
        @{
            Name = 'hk_service_check'
            Includes = @('tests/host/eps_mode/include', 'tests/host/startup_services/include',
                         'components/hk', 'components/adcs', 'components/eps', 'components/comms',
                         'components/system_mode', 'components/fsw_common')
            Sources = @('tests/host/hk_service/check.c', 'components/hk/hk_service.c')
            LinkFlags = @()
            Cases = @('normal', 'init_allocation', 'init_lock', 'init_unlock',
                      'init_lock_preserves_seed', 'init_reuses_mutex')
        },
        @{
            Name = 'tm_gen_check'
            Includes = @('tests/host/tm_gen/include', 'components/cdh', 'components/hk', 'components/system_mode')
            Sources = @('tests/host/tm_gen/check.c', 'components/cdh/tm_gen.c')
            LinkFlags = @('-fno-builtin-printf', '-Wl,--wrap=printf')
        },
        @{
            Name = 'adcs_math_check'
            Includes = @('components/adcs')
            Sources = @('tests/host/adcs_math/check.c', 'components/adcs/adcs_math.c', 'components/adcs/adcs_control.c')
            LinkFlags = @('-lm')
        },
        @{
            Name = 'adcs_sim_check'
            Includes = @('components/adcs')
            Sources = @('tests/host/adcs_sim/check.c', 'components/adcs/adcs_sim.c', 'components/adcs/adcs_math.c')
            LinkFlags = @('-lm')
        },
        @{
            Name = 'radio_link_check'
            Includes = @('components/radio_link', 'components/comms', 'components/time', 'components/cdh')
            Sources = @('tests/host/radio_link/check.c', 'components/radio_link/comms_frame.c',
                        'components/radio_link/tm_manager.c', 'components/comms/ccsds_packet.c')
            LinkFlags = @('-DSI_TEST_MODE=1', '-fno-builtin-printf', '-Wl,--wrap=printf')
        },
        @{
            Name = 'mission_contract_check'
            Includes = @('components/fsw_common', 'components/comms')
            Sources = @('tests/host/mission_contract/check.c')
            LinkFlags = @()
        },
        @{
            Name = 'hal_check'
            Includes = @('components/sat_hal')
            Sources = @('tests/host/hal/check.c', 'components/sat_hal/hal.c')
            LinkFlags = @()
        },
        @{
            Name = 'watchdog_recovery_injection_check'
            Includes = @('tests/host/watchdog_injection/include', 'tests/host/watchdog/include',
                         'components/watchdog', 'components/system_mode', 'components/fdir')
            Sources = @('tests/host/watchdog_recovery/check.c', 'components/watchdog/watchdog_recovery_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_TASK_RECOVERY=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'drop_first_once', 'other_roles',
                      'first_resume', 'first_wait_ground', 'second_dropout', 'second_resume', 'complete',
                      'complete_no_reinject', 'snapshot_failure', 'other_cause', 'fatal', 'uncertain', 'overflow', 'wrong_clear_mode')
        },
        @{
            Name = 'watchdog_task_injection_check'
            Includes = @('tests/host/watchdog_injection/include', 'tests/host/watchdog/include', 'tests/host/watchdog_port/include',
                         'components/watchdog', 'components/system_mode', 'components/fdir')
            Sources = @('tests/host/watchdog_injection/check.c', 'components/watchdog/watchdog_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_TASK_LOSS=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'drop_target', 'drop_once', 'other_roles',
                      'active_snapshot', 'empty_snapshot', 'snapshot_failure', 'fatal_not_match', 'wrong_key_no_match')
        },
        @{
            Name = 'watchdog_stall_injection_check'
            Includes = @('tests/host/watchdog_injection/include', 'tests/host/watchdog/include', 'tests/host/watchdog_port/include',
                         'components/watchdog', 'components/system_mode', 'components/fdir')
            Sources = @('tests/host/watchdog_injection/check.c', 'components/watchdog/watchdog_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_SUPERVISOR_STALL=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'all_pets_preserved', 'recovery_suppressed',
                      'sixth_check', 'wrong_reason_armed', 'unexpected_resume')
        },
        @{
            Name = 'watchdog_port_check'
            Includes = @('tests/host/watchdog_port/include', 'components/watchdog')
            Sources = @('tests/host/watchdog_port/check.c', 'components/watchdog/watchdog_port_esp32.c')
            LinkFlags = @()
            Cases = @('configure_success', 'configure_existing', 'configure_uninitialized', 'configure_status_error',
                      'configure_error', 'subscribe_success', 'subscribe_duplicate', 'subscribe_uninitialized',
                      'subscribe_no_memory', 'feed_success', 'feed_unregistered', 'feed_uninitialized', 'feed_error')
        },
        @{
            Name = 'watchdog_idle_starvation_check'
            Includes = @('tests/host/watchdog_starvation/include', 'tests/host/watchdog/include',
                         'tests/host/watchdog_port/include', 'components/watchdog', 'components/system_mode', 'components/fdir')
            Sources = @('tests/host/watchdog_starvation/check.c', 'components/watchdog/watchdog_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_IDLE_STARVATION=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort', '-Wl,--wrap=fflush')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'all_pets_preserved', 'recovery_suppressed',
                      'wrong_reason_armed', 'sixth_check', 'create_failure', 'worker_order', 'flush_failure')
        },
        @{
            Name = 'watchdog_interrupt_starvation_check'
            Includes = @('tests/host/watchdog_starvation/include', 'tests/host/watchdog/include',
                         'tests/host/watchdog_port/include', 'components/watchdog', 'components/system_mode', 'components/fdir')
            Sources = @('tests/host/watchdog_starvation/check.c', 'components/watchdog/watchdog_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_INTERRUPT_MASK=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort', '-Wl,--wrap=fflush')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'all_pets_preserved', 'recovery_suppressed',
                      'wrong_reason_armed', 'sixth_check', 'create_failure', 'worker_order', 'flush_failure')
        },
        @{
            Name = 'watchdog_idle_starvation_cpu1_check'
            Includes = @('tests/host/watchdog_starvation/include', 'tests/host/watchdog/include',
                         'tests/host/watchdog_port/include', 'components/watchdog', 'components/system_mode', 'components/fdir')
            Sources = @('tests/host/watchdog_starvation/check.c', 'components/watchdog/watchdog_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_IDLE_STARVATION=1', '-DFSW_TEST_WDT_CORE=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort', '-Wl,--wrap=fflush')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'all_pets_preserved', 'recovery_suppressed',
                      'wrong_reason_armed', 'sixth_check', 'create_failure', 'worker_order', 'flush_failure')
        },
        @{
            Name = 'watchdog_interrupt_starvation_cpu1_check'
            Includes = @('tests/host/watchdog_starvation/include', 'tests/host/watchdog/include',
                         'tests/host/watchdog_port/include', 'components/watchdog', 'components/system_mode', 'components/fdir')
            Sources = @('tests/host/watchdog_starvation/check.c', 'components/watchdog/watchdog_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_INTERRUPT_MASK=1', '-DFSW_TEST_WDT_CORE=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort', '-Wl,--wrap=fflush')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'all_pets_preserved', 'recovery_suppressed',
                      'wrong_reason_armed', 'sixth_check', 'create_failure', 'worker_order', 'flush_failure')
        },
        @{
            Name = 'watchdog_global_starvation_check'
            Includes = @('tests/host/watchdog_starvation/include', 'tests/host/watchdog/include',
                         'tests/host/watchdog_port/include', 'components/watchdog')
            Sources = @('tests/host/watchdog_starvation/global_check.c', 'components/watchdog/watchdog_global_test_esp32.c')
            LinkFlags = @('-DFSW_TEST_WDT_GLOBAL_STARVATION=1', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=abort', '-Wl,--wrap=fflush')
            Cases = @('uninitialized', 'initialized', 'repeat_init', 'all_pets_preserved', 'recovery_suppressed',
                      'wrong_reason_armed', 'sixth_check', 'first_create_failure', 'second_create_failure',
                      'ready_timeout', 'partial_ready', 'affinity_failure', 'flush_failure', 'worker0_order', 'worker1_order')
        },
        @{
            Name = 'watchdog_check'
            Includes = @('tests/host/watchdog/include', 'components/watchdog',
                         'components/fdir', 'components/system_mode', 'components/time', 'components/comms')
            Sources = @('tests/host/watchdog/check.c', 'components/watchdog/watchdog.c',
                        'components/fdir/fdir_service.c', 'components/system_mode/system_mode.c')
            LinkFlags = @('-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=FDIR_ReportFault',
                          '-Wl,--wrap=abort', '-Wl,--wrap=FDIR_TryGetFaultRevision',
                          '-Wl,--wrap=FDIR_TryResolveCriticalFault')
            Cases = @(
                foreach ($task in 0..6) {
                    foreach ($boundary in @('below', 'at', 'above')) { 'boundary_' + $task + '_' + $boundary }
                    'wrap_' + $task
                    'latch_' + $task
                }
                foreach ($mode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) { 'mode_' + $mode }
                'uninitialized', 'unarmed', 'allocation_failure', 'arm_lock_failure', 'repeat_init', 'repeat_arm',
                'invalid_ids', 'startup_grace', 'future_pet_before_check', 'pet_after_timeout_commit',
                'mutex_check_failure', 'mutex_pet_failure', 'mutex_failure_once', 'mode_record_failure',
                'all_missing', 'multiple_missing', 'ack_retains', 'ledger_full', 'monitor_period', 'monitor_no_reset'
                'sdk_config_failure', 'sdk_config_repeat_failure', 'monitor_subscribe_failure', 'monitor_feed_failure',
                'monitor_unarmed', 'monitor_healthy', 'monitor_check_recovers', 'monitor_check_stalls', 'monitor_report_stall'
                'timing_unarmed', 'timing_null', 'timing_lock_failure', 'timing_initial',
                'timing_intervals', 'timing_zero', 'timing_wrap', 'timing_repeat_arm'
                foreach ($task in 0..6) {
                    'recovery_task_' + $task
                    'recovery_relapse_' + $task
                    'recovery_boundary_' + $task
                    'recovery_wrap_' + $task
                }
                'recovery_no_resume', 'recovery_no_record', 'recovery_pre_revision_pets',
                'recovery_unstable_gap', 'recovery_gap_at_timeout', 'recovery_stale', 'recovery_fresh_at_boundary',
                'recovery_same_tick_burst', 'recovery_revision_change', 'recovery_revision_query_failure',
                'recovery_query_lock_failure', 'recovery_resolve_lock_failure', 'recovery_rereport_before_resolve',
                'recovery_fatal_before_resolve', 'recovery_rereport_after_resolve', 'recovery_other_source',
                'recovery_other_fatal', 'recovery_same_fatal', 'recovery_overflow', 'recovery_uncertainty',
                'recovery_state_fault', 'recovery_other_task', 'recovery_ack_no_acceleration',
                'recovery_mode_critical', 'recovery_mode_emergency', 'recovery_predeploy'
            )
        },
        @{
            Name = 'fdir_faults_check'
            Includes = @('tests/host/eps_mode/include', 'components/system_mode', 'components/fdir',
                         'components/eps', 'components/cdh', 'components/archive', 'components/time', 'components/comms')
            Sources = @('tests/host/fdir_faults/check.c', 'components/system_mode/system_mode.c',
                        'components/fdir/fdir_service.c', 'components/eps/eps_logic.c', 'components/eps/eps_commands.c',
                        'components/archive/archive_service.c', 'components/archive/utils.c')
            LinkFlags = @('-lm', '-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=SYSTEM_MODE_WithCurrent',
                          '-Wl,--wrap=FDIR_GetFaultRevision', '-Wl,--wrap=FDIR_ResolveCriticalFault',
                          '-DFSW_TEST_ARCHIVE_CORRUPTION=1')
            Cases = @(
                foreach ($level in @('critical', 'fatal')) {
                    foreach ($api in @('set', 'transition', 'quiet', 'ground')) {
                        foreach ($mode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                            'gate_' + $level + '_' + $api + '_' + $mode
                        }
                    }
                    foreach ($source in @('payload', 'eps', 'comms', 'archive', 'cdh', 'adcs')) { 'source_' + $source + '_' + $level }
                    foreach ($operation in 0..3) { 'operation_' + $level + '_' + $operation }
                }
                foreach ($health in @('below', 'at', 'above', 'six', 'low', 'zero', 'negative', 'nan', 'inf', 'neg_inf')) {
                    'health_' + $health
                }
                'same_source_codes', 'same_code_sources', 'repeat_reports', 'severity_promotion', 'ack_retains',
                'snapshot_null', 'snapshot_lock', 'revision_query', 'query_unknown', 'resolve_wrong_revision',
                'resolve_rereport', 'resolve_reuse', 'resolve_lock', 'fatal_not_resolved', 'overflow_critical',
                'overflow_fatal', 'full_duplicate', 'overflow_after_resolve', 'record_lock_critical', 'record_lock_fatal',
                'uninitialized_critical', 'uninitialized_fatal', 'repeat_init', 'quiet_no_log',
                'eps_revision_failure', 'eps_clear_failure', 'eps_rereport_before_clear', 'eps_report_after_clear',
                'eps_fatal_same_code', 'eps_other_fault_retained', 'eps_mutex_failure', 'eps_relapse', 'eps_explicit_recovery',
                'real_archive_crc', 'before_commit_nominal', 'predeploy_critical', 'predeploy_fatal',
                'predeploy_failure_critical', 'predeploy_failure_fatal'
                'try_query', 'try_query_busy', 'try_query_invalid', 'try_resolve', 'try_resolve_busy',
                'try_resolve_stale', 'try_resolve_fatal', 'try_resolve_invalid', 'try_preserves_uncertainty', 'try_uninitialized'
            )
        },
        @{
            Name = 'fdir_check'
            Includes = @('tests/host/fdir/include', 'tests/host/eps_mode/include', 'components/system_mode',
                         'main', 'components/watchdog', 'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/fdir/check.c', 'components/system_mode/system_mode.c',
                        'components/fdir/fdir_service.c', 'main/fdir_task.c')
            LinkFlags = @('-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=FDIR_Process')
            Cases = @(
                foreach ($mode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                    foreach ($count in 0..4) { 'mode_' + $mode + '_count_' + $count }
                }
                foreach ($source in @('payload', 'eps', 'comms', 'archive', 'cdh', 'adcs')) { 'source_' + $source }
                'fdir_uninitialized', 'allocation_failure', 'repeat_init', 'mode_uninitialized', 'quiet_contract',
                'pre_deployment_ground',
                'task_nominal', 'task_boot_deployment', 'task_ack', 'task_mode_retry', 'task_counter_retry', 'task_safe',
                'task_stack_periodic',
                'invalid_reports', 'info_not_warning', 'non_eps_critical', 'empty_commands', 'multiple_sources',
                'partial_sources', 'repeated_batches', 'saturation', 'mode_lock_retry', 'persistent_mode_lock',
                'counter_lock_retry', 'ack_before_process', 'ack_at_boundary', 'ack_unavailable', 'ack_after_commit',
                'new_warning_after_commit', 'emergency_after_commit', 'safe_before_commit', 'critical_before_commit',
                'emergency_before_commit', 'degraded_before_commit', 'fatal_keeps_warning', 'eps_critical_keeps_warning',
                'ack_does_not_recover', 'query_lock_failure'
            )
        },
        @{
            Name = 'deployment_check'
            Includes = @('tests/host/deployment/include', 'tests/host/eps_mode/include', 'components/system_mode',
                         'components/cdh', 'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/deployment/check.c', 'components/system_mode/system_mode.c',
                        'components/cdh/deployment_service.c', 'components/fdir/fdir_service.c')
            LinkFlags = @('-fno-builtin-printf', '-Wl,--wrap=printf', '-Wl,--wrap=SYSTEM_MODE_CompleteDeployment')
            Cases = @(
                foreach ($phase in @('before', 'after')) {
                    foreach ($api in @('set', 'transition', 'ground')) {
                        foreach ($fromMode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                            if ($phase -eq 'before' -and $fromMode -in @('nominal', 'degraded')) { continue }
                            foreach ($toMode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                                $phase + '_' + $api + '_' + $fromMode + '_' + $toMode
                            }
                        }
                    }
                }
                foreach ($completeMode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                    'complete_' + $completeMode
                }
                'task_boot', 'task_safe', 'task_emergency', 'task_critical', 'task_fault_silence', 'task_fatal_burn',
                'task_retry', 'task_fault_retry', 'task_fault_before_commit', 'task_fault_after_commit', 'task_still_retrying',
                'completion_lock_failure', 'query_lock_failure', 'idempotent_boot_return', 'repeat_init',
                'ack_not_deployment', 'invalid_modes', 'mutation_lock_failure', 'ground_missing_mode', 'with_current',
                'critical_fault_preserved', 'fatal_fault_preserved',
                'uninitialized', 'init_reject_nominal', 'init_reject_degraded', 'init_allocation_failure'
            )
        },
        @{
            Name = 'subsystem_safety_check'
            Includes = @('tests/host/eps_mode/include', 'components/eps', 'components/adcs',
                         'components/payload', 'components/archive', 'components/cdh', 'components/system_mode',
                         'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/subsystem_safety/check.c', 'components/eps/eps_logic.c', 'components/eps/eps_commands.c',
                        'components/system_mode/system_mode.c', 'components/fdir/fdir_service.c',
                        'components/cdh/deployment_service.c',
                        'components/adcs/adcs_service.c', 'components/adcs/adcs_manager.c', 'components/adcs/adcs_sim.c',
                        'components/adcs/adcs_control.c', 'components/adcs/adcs_math.c', 'components/payload/payload_service.c')
            LinkFlags = @('-lm', '-fno-builtin-printf',
                          '-Wl,--wrap=printf', '-Wl,--wrap=abort', '-Wl,--wrap=SYSTEM_MODE_WithCurrent',
                          '-Wl,--wrap=FDIR_ReportFault', '-Wl,--wrap=SIM_ReadGyroscope', '-Wl,--wrap=SIM_ReadMagnetometer',
                          '-Wl,--wrap=SIM_GetAltitude', '-Wl,--wrap=SIM_ApplyBoost', '-Wl,--wrap=SIM_UpdatePhysics')
            Cases = @(
                foreach ($globalMode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                    foreach ($epsMode in @('nominal', 'low', 'safe', 'recovery')) {
                        foreach ($operation in @('detumble', 'pointing', 'boost', 'payload')) {
                            'permit_' + $globalMode + '_' + $epsMode + '_' + $operation
                        }
                    }
                    foreach ($control in @('detumble', 'pointing')) { 'adcs_' + $globalMode + '_' + $control }
                    foreach ($action in @('start', 'update')) { 'payload_' + $globalMode + '_' + $action }
                    'boost_' + $globalMode
                }
                'permission_invalid', 'permission_pdu', 'permission_voltage', 'permission_flag', 'permission_nan', 'permission_infinity',
                'permission_eps_lock', 'permission_mode_lock', 'permission_null_mutex', 'permission_uninitialized',
                'adcs_stale_torque', 'adcs_stale_dipole', 'adcs_publish_race', 'adcs_physics_race', 'adcs_boost_race',
                'adcs_invalid_modes', 'adcs_saturation', 'adcs_nan', 'adcs_stable_boot', 'adcs_state_lock',
                'adcs_rate_below', 'adcs_rate_at', 'adcs_rate_above', 'adcs_rate_negative',
                'adcs_nominal_saturation', 'adcs_nominal_damping', 'adcs_invalid_dt',
                'manager_safe', 'manager_boost', 'manager_insufficient_fuel',
                'payload_recovery', 'payload_prepare_race', 'payload_start_race', 'payload_commit_race',
                'payload_power_cut', 'payload_state_lock', 'payload_archive_failure', 'payload_ack_nack',
                'payload_stop_reset', 'payload_rate_invalid', 'sim_boost_parameters', 'adcs_init_failure', 'payload_init_failure',
                'adcs_init_lock_failure', 'payload_init_lock_failure',
                'lifecycle_boot_nominal', 'lifecycle_predeploy_clear_first', 'lifecycle_predeploy_complete_first',
                'lifecycle_runtime_recovery', 'lifecycle_runtime_degraded', 'lifecycle_other_critical',
                'lifecycle_other_fatal', 'lifecycle_record_uncertain', 'lifecycle_pdu_off',
                'lifecycle_recovery_relapse', 'lifecycle_preinit_critical', 'lifecycle_preinit_fatal'
            )
        },
        @{
            Name = 'eps_loads_check'
            Includes = @('tests/host/eps_mode/include', 'components/eps', 'components/cdh',
                         'components/system_mode', 'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/eps_loads/check.c', 'components/eps/eps_logic.c',
                        'components/eps/eps_commands.c', 'components/system_mode/system_mode.c',
                        'components/fdir/fdir_service.c')
            LinkFlags = @('-Wl,--wrap=SYSTEM_MODE_WithCurrent', '-Wl,--wrap=EPS_Monitor',
                          '-Wl,--wrap=FDIR_ResolveCriticalFault',
                          '-Wl,--wrap=FDIR_ReportFault', '-Wl,--wrap=printf', '-fno-builtin-printf',
                          '-lm')
            Cases = @(
                foreach ($globalMode in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                    foreach ($localMode in @('nominal', 'low', 'safe', 'recovery')) {
                        foreach ($voltageCase in @('low', 'at_low', 'mid', 'below_recovery', 'at_recovery',
                            'just_below_low', 'just_above_low', 'just_below_recovery', 'just_above_recovery',
                            'nan', 'pos_inf', 'neg_inf')) {
                            $globalMode + '_' + $localMode + '_' + $voltageCase
                        }
                    }
                }
                'uninitialized_mode', 'mode_lock_failure', 'mode_lock_retry', 'mode_lock_low_voltage',
                'eps_lock_failure', 'eps_lock_retry',
                'getter_lock_failure', 'getter_null_mutex', 'null_callback', 'uninitialized_callback', 'callback_contract',
                'emergency_before_restore', 'critical_before_restore', 'safe_before_restore', 'safety_after_commit',
                'recovery_lifecycle', 'recovery_relapse', 'boot_preserve_off', 'comms_off_preserved', 'unrelated_bits',
                'repeated_safe', 'heater_stub', 'monitor_3_3', 'soc_sunrise',
                'soc_boundaries', 'null_commands', 'threshold_chatter', 'invalid_recovery'
            )
        },
        @{
            Name = 'eps_mode_check'
            Includes = @('tests/host/eps_mode/include', 'components/cdh', 'components/eps',
                         'components/system_mode', 'components/fdir', 'components/time', 'components/comms')
            Sources = @('tests/host/eps_mode/check.c', 'components/cdh/eps_control.c',
                        'components/system_mode/system_mode.c', 'components/fdir/fdir_service.c')
            LinkFlags = @('-Wl,--wrap=FDIR_ReportFault', '-Wl,--wrap=SYSTEM_MODE_Transition',
                          '-Wl,--wrap=SYSTEM_MODE_RecordFault',
                          '-Wl,--wrap=SYSTEM_MODE_Set', '-Wl,--wrap=printf', '-fno-builtin-printf', '-lm')
            Cases = @(
                      foreach ($voltageCase in @('just_below_low', 'just_above_low',
                          'just_below_recovery', 'just_above_recovery', 'nan', 'pos_inf', 'neg_inf')) {
                          foreach ($modeCase in @('boot', 'safe', 'nominal', 'degraded', 'emergency', 'critical')) {
                              $voltageCase + '_' + $modeCase
                          }
                      }
                      'low_boot', 'low_safe', 'low_nominal', 'low_degraded', 'low_emergency', 'low_critical',
                      'at_low_boot', 'at_low_safe', 'at_low_nominal', 'at_low_degraded', 'at_low_emergency', 'at_low_critical',
                      'at_recovery_boot', 'at_recovery_safe', 'at_recovery_nominal', 'at_recovery_degraded',
                      'at_recovery_emergency', 'at_recovery_critical',
                      'recovery_boot', 'recovery_safe', 'recovery_nominal', 'recovery_degraded',
                      'recovery_emergency', 'recovery_critical',
                      'eps_mutex_failure', 'uninitialized_mode', 'critical_lock_failure', 'recovery_lock_failure',
                      'emergency_during_report', 'emergency_before_fdir_safe', 'emergency_before_critical',
                      'emergency_before_recovery', 'critical_changed_before_transition',
                      'recovery_changed_before_transition', 'critical_retry', 'recovery_retry')
        },
        @{
            Name = 'startup_core_check'
            Includes = @('tests/host/startup_core/include', 'main', 'components/time',
                         'components/system_mode', 'components/fdir', 'components/comms',
                         'components/archive', 'components/hk', 'components/payload',
                         'components/adcs', 'components/cdh', 'components/eps')
            Sources = @('tests/host/startup_core/check.c', 'main/startup_core.c',
                        'main/startup_resources.c', 'main/main.c')
            LinkFlags = @('-Wl,--wrap=abort', '-Wl,--wrap=printf', '-fno-builtin-printf')
            Cases = @('success', 'mode', 'fdir', 'watchdog', 'sender', 'archive', 'hk', 'tm_queue', 'tc_queue', 'eps_mutex', 'watchdog_arm',
                      'offline', 'no_log', 'resource_monitor_failure', 'repeat_success', 'repeat_mode', 'repeat_fdir', 'repeat_watchdog', 'repeat_sender', 'repeat_archive', 'repeat_hk',
                      'task_WDT_MON', 'task_DEPLOY', 'task_EPS_MGMT', 'task_CMD_PROC', 'task_TC_RX',
                      'task_EPS_MON', 'task_FDIR', 'task_DATA_LOG', 'task_ADCS_MGMT', 'task_TM_GEN',
                      'task_CMD_INJECT', 'task_SOLAR_HARV', 'early_success',
                      'partial_task_WDT_MON', 'partial_task_DEPLOY', 'partial_task_EPS_MGMT',
                      'partial_task_CMD_PROC', 'partial_task_TC_RX', 'partial_task_EPS_MON',
                      'partial_task_FDIR', 'partial_task_DATA_LOG', 'partial_task_ADCS_MGMT',
                      'partial_task_TM_GEN', 'partial_task_CMD_INJECT', 'partial_task_SOLAR_HARV',
                      'repeat_partial_task_WDT_MON', 'repeat_partial_task_DEPLOY', 'repeat_partial_task_EPS_MGMT',
                      'repeat_partial_task_CMD_PROC', 'repeat_partial_task_TC_RX', 'repeat_partial_task_EPS_MON',
                      'repeat_partial_task_FDIR', 'repeat_partial_task_DATA_LOG', 'repeat_partial_task_ADCS_MGMT',
                      'repeat_partial_task_TM_GEN', 'repeat_partial_task_CMD_INJECT', 'repeat_partial_task_SOLAR_HARV')
        },
        @{
            Name = 'wifi_service_check'
            Includes = @('tests/host/wifi_service/include', 'main', 'components/comms')
            Sources = @('tests/host/wifi_service/check.c', 'main/wifi_service.c', 'components/comms/link_retry.c')
            LinkFlags = @()
            Cases = @('nvs', 'stack', 'event', 'credentials', 'existing', 'allocation', 'attach',
                      'defaults', 'group', 'init', 'storage', 'wifi-handler', 'ip-handler', 'mode',
                      'config', 'start', 'task', 'stop-cleanup', 'deinit-cleanup', 'ip-unregister',
                      'wifi-unregister', 'clear-cleanup', 'timeout', 'runtime', 'connected')
        },
        @{
            Name = 'yamcs_log_check'
            Includes = @('tests/host/wifi_service/include', 'components/comms')
            Sources = @('tests/host/yamcs_log/check.c', 'components/comms/yamcs_log.c')
            LinkFlags = @('-D_POSIX_C_SOURCE=200809L')
        },
        @{
            Name = 'link_retry_check'
            Includes = @('components/comms')
            Sources = @('tests/host/link_retry/check.c', 'components/comms/link_retry.c')
            LinkFlags = @()
        },
        @{
            Name = 'tc_frame_check'
            Includes = @('tests/host/tc_frame/include', 'components/cdh', 'components/comms',
                         'components/adcs', 'components/eps', 'components/hk', 'components/archive',
                         'components/fdir', 'components/payload', 'components/time', 'components/recovery')
            Sources = @('tests/host/tc_frame/check.c', 'components/cdh/tc_proc.c',
                        'components/cdh/cdhs_router.c', 'components/cdh/utils.c')
            LinkFlags = @('-Wl,--wrap=crc16_ccitt')
        },
        @{
            Name = 'archive_read_check'
            Includes = @('tests/host/eps_mode/include', 'components/archive', 'components/fsw_common', 'components/fdir', 'components/time',
                         'components/comms')
            Sources = @('tests/host/archive_read/read_bounds_check.c',
                        'tests/host/archive_sync/mutex_mock.c', 'components/archive/archive_service.c', 'components/archive/utils.c')
            LinkFlags = @('-Wl,--wrap=utils_crc16', '-pthread', '-D_POSIX_C_SOURCE=200809L', '-DFSW_TEST_ARCHIVE_CORRUPTION=1')
        },
        @{
            Name = 'archive_downlink_check'
            Includes = @('tests/host/eps_mode/include', 'components/archive', 'components/fsw_common', 'components/fdir', 'components/time',
                         'components/comms')
            Sources = @('tests/host/archive_downlink/downlink_check.c',
                        'tests/host/archive_sync/mutex_mock.c', 'components/archive/archive_service.c', 'components/archive/utils.c')
            LinkFlags = @('-pthread', '-D_POSIX_C_SOURCE=200809L', '-DFSW_TEST_ARCHIVE_CORRUPTION=1')
        },
        @{
            Name = 'yamcs_sender_check'
            Includes = @('tests/host/yamcs_sender/include', 'components/comms')
            Sources = @('tests/host/yamcs_sender/sender_check.c', 'components/comms/yamcs_interface.c')
            LinkFlags = @('-pthread', '-D_POSIX_C_SOURCE=200809L', '-Wl,--wrap=vsnprintf')
        },
        @{
            Name = 'startup_services_check'
            Includes = @('tests/host/eps_mode/include', 'tests/host/startup_services/include', 'components/hk', 'components/payload',
                         'components/archive', 'components/cdh', 'components/comms', 'components/adcs',
                         'components/eps', 'components/fdir', 'components/time', 'components/system_mode')
            Sources = @('tests/host/startup_services/check.c', 'components/hk/hk_service.c',
                        'components/payload/payload_service.c')
            LinkFlags = @()
        },
        @{
            Name = 'adcs_startup_check'
            Includes = @('tests/host/eps_mode/include', 'tests/host/adcs_startup/include', 'components/adcs', 'components/cdh',
                         'components/eps', 'components/fdir', 'components/time', 'components/system_mode')
            Sources = @('tests/host/adcs_startup/check.c', 'components/adcs/adcs_service.c',
                        'components/adcs/adcs_manager.c', 'components/adcs/adcs_sim.c',
                        'components/adcs/adcs_control.c', 'components/adcs/adcs_math.c')
            LinkFlags = @('-lm', '-Wl,--wrap=ADCS_Init', '-Wl,--wrap=SIM_SetTrueAngularVelocity')
        },
        @{
            Name = 'startup_resources_check'
            Includes = @('tests/host/startup_resources/include', 'main', 'components/cdh')
            Sources = @('tests/host/startup_resources/check.c', 'main/startup_resources.c')
            LinkFlags = @('-Wl,--wrap=abort')
            Cases = @('normal', 'telemetry_queue', 'command_queue', 'eps_mutex', 'null',
                      'repeat_normal', 'repeat_normal_other', 'repeat_normal_null',
                      'repeat_telemetry_queue', 'repeat_command_queue', 'repeat_eps_mutex', 'repeat_null')
        },
        @{
            Name = 'network_startup_check'
            Includes = @('tests/host/network_startup/include', 'components/cdh', 'components/comms', 'components/time', 'main')
            Sources = @('tests/host/network_startup/check.c', 'components/cdh/tc_udp_rx.c',
                        'components/comms/yamcs_interface.c', 'components/comms/ccsds_packet.c', 'main/startup_network.c')
            LinkFlags = @('-D_POSIX_C_SOURCE=200809L', '-Wl,--wrap=abort')
        },
        @{
            Name = 'startup_nvs_check'
            Includes = @('tests/host/startup_nvs/include', 'main')
            Sources = @('tests/host/startup_nvs/check.c', 'main/startup_nvs.c')
            LinkFlags = @()
            Cases = @('0', '1', '2', '3', '4', '5', '6')
        }
    )

    # Runtime fixtures may compile several services together. Header isolation above
    # is separate and never inherits these broader fixture paths.
    foreach ($hostTest in $hostSuiteTests) {
        if ('components/archive/archive_service.c' -in $hostTest.Sources) {
            $hostTest.Sources += 'components/archive/archive_transfer_wire.c'
        }
        if ('components/cdh' -in $hostTest.Includes -and 'components/watchdog' -notin $hostTest.Includes) {
            $hostTest.Includes += 'components/watchdog'
        }
        if (('components/cdh' -in $hostTest.Includes -or 'components/comms' -in $hostTest.Includes) -and
            'components/fsw_common' -notin $hostTest.Includes) {
            $hostTest.Includes += 'components/fsw_common'
        }
        if ('components/watchdog' -in $hostTest.Includes -and 'components/fsw_common' -notin $hostTest.Includes) {
            $hostTest.Includes += 'components/fsw_common'
        }
        if ('components/fdir' -in $hostTest.Includes -and 'components/system_mode' -notin $hostTest.Includes) {
            $hostTest.Includes += 'components/system_mode'
        }
    }
    return @{ Tests = @($hostSuiteTests); Headers = @($headerChecks) }
}
