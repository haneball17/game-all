use crate::{InjectorConfig, InjectorConfigInterop};
use game_core_protocols::{HELPER_STATUS_V5_SIZE, HELPER_STATUS_V5_VERSION};
use std::collections::BTreeMap;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InjectorRuntimePlan {
    pub config: InjectorConfig,
    pub helper_status_version: u32,
    pub helper_status_size: u32,
}

impl InjectorRuntimePlan {
    pub fn from_ini_text(text: &str, base_dir: &str) -> Self {
        let mut config = InjectorConfig::parse_ini(text);
        config.normalize_paths(base_dir);
        Self {
            config,
            helper_status_version: HELPER_STATUS_V5_VERSION,
            helper_status_size: HELPER_STATUS_V5_SIZE,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HelperHeartbeatDecision {
    pub contract_ok: bool,
    pub heartbeat_ok: bool,
}

pub fn evaluate_helper_heartbeat(
    status_version: u32,
    status_size: u32,
    process_alive: bool,
    last_tick: u64,
    now_tick: u64,
    timeout_ms: u64,
) -> HelperHeartbeatDecision {
    let contract_ok =
        status_version == HELPER_STATUS_V5_VERSION && status_size == HELPER_STATUS_V5_SIZE;
    let heartbeat_ok = contract_ok
        && process_alive
        && now_tick >= last_tick
        && now_tick.saturating_sub(last_tick) <= timeout_ms;
    HelperHeartbeatDecision {
        contract_ok,
        heartbeat_ok,
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectionRetryDecision {
    pub attempt: u32,
    pub backend_started: bool,
    pub success_by_file: bool,
    pub success_by_heartbeat: bool,
    pub success_source: u32,
    pub used_heartbeat_fallback: bool,
    pub succeeded: bool,
    pub should_retry: bool,
    pub retry_delay_ms: u32,
    pub finished: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectionRetryRuntime {
    max_retries: u32,
    retry_interval_ms: u32,
    current_attempt: u32,
    finished: bool,
}

impl InjectionRetryRuntime {
    pub fn new(max_retries: u32, retry_interval_ms: u32) -> Self {
        Self {
            max_retries,
            retry_interval_ms,
            current_attempt: 1,
            finished: max_retries == 0,
        }
    }

    pub fn can_attempt(&self) -> bool {
        !self.finished && self.current_attempt <= self.max_retries
    }

    pub fn current_attempt(&self) -> u32 {
        self.current_attempt
    }

    pub fn finish_attempt(
        &mut self,
        apc_queued: bool,
        success_by_file: bool,
        success_by_heartbeat: bool,
    ) -> InjectionRetryDecision {
        let attempt = self.current_attempt;
        let succeeded = success_by_file || success_by_heartbeat;

        let should_retry = !succeeded && apc_queued && attempt < self.max_retries;
        if should_retry {
            self.current_attempt = self.current_attempt.saturating_add(1);
        } else {
            self.finished = true;
        }

        InjectionRetryDecision {
            attempt,
            backend_started: apc_queued,
            success_by_file,
            success_by_heartbeat,
            success_source: if success_by_file {
                1
            } else if success_by_heartbeat {
                2
            } else {
                0
            },
            used_heartbeat_fallback: apc_queued && !success_by_file,
            succeeded,
            should_retry,
            retry_delay_ms: if should_retry { self.retry_interval_ms } else { 0 },
            finished: self.finished,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct WindowProbeResult {
    pub ok: bool,
    pub timed_out: bool,
    pub used_hint: bool,
    pub error_code: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SuccessObservationResult {
    pub observed: bool,
    pub timed_out: bool,
    pub used_fallback: bool,
    pub error_code: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HeartbeatObservationResult {
    pub observed: bool,
    pub contract_ok: bool,
    pub mapping_found: bool,
    pub timed_out: bool,
    pub error_code: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BackendExecutionResult {
    pub started: bool,
    pub configured_backend: u32,
    pub effective_backend: u32,
    pub downgraded: bool,
    pub error_code: u32,
}

pub fn finish_attempt_with_observation(
    runtime: &mut InjectionRetryRuntime,
    backend: BackendExecutionResult,
    success: SuccessObservationResult,
    heartbeat: HeartbeatObservationResult,
) -> InjectionRetryDecision {
    runtime.finish_attempt(backend.started, success.observed, heartbeat.observed)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WatchTaskState {
    Pending,
    Running,
    Succeeded,
    Failed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct WatchTaskRecord {
    present: bool,
    last_seen_tick: u64,
    state: WatchTaskState,
}

#[derive(Debug, Clone)]
pub struct InjectorWatchRuntime {
    idle_exit_seconds: u32,
    last_new_tick: u64,
    tasks: BTreeMap<u32, WatchTaskRecord>,
}

impl InjectorWatchRuntime {
    pub fn new(config: &InjectorConfig, now_tick: u64) -> Self {
        Self {
            idle_exit_seconds: config.idle_exit_seconds,
            last_new_tick: now_tick,
            tasks: BTreeMap::new(),
        }
    }

    pub fn observe_processes(&mut self, now_tick: u64, pids: &[u32]) {
        for task in self.tasks.values_mut() {
            task.present = false;
        }

        for &pid in pids {
            use std::collections::btree_map::Entry;
            match self.tasks.entry(pid) {
                Entry::Vacant(entry) => {
                    entry.insert(WatchTaskRecord {
                        present: true,
                        last_seen_tick: now_tick,
                        state: WatchTaskState::Pending,
                    });
                    self.last_new_tick = now_tick;
                }
                Entry::Occupied(mut entry) => {
                    let task = entry.get_mut();
                    task.present = true;
                    task.last_seen_tick = now_tick;
                }
            }
        }
    }

    pub fn collect_pending(&self, limit: usize) -> Vec<u32> {
        self.tasks
            .iter()
            .filter_map(|(&pid, task)| {
                if task.present && task.state == WatchTaskState::Pending {
                    Some(pid)
                } else {
                    None
                }
            })
            .take(limit)
            .collect()
    }

    pub fn mark_started(&mut self, pid: u32) -> bool {
        let Some(task) = self.tasks.get_mut(&pid) else {
            return false;
        };
        if task.state != WatchTaskState::Pending {
            return false;
        }
        task.state = WatchTaskState::Running;
        true
    }

    pub fn mark_finished(&mut self, pid: u32, succeeded: bool) -> bool {
        let Some(task) = self.tasks.get_mut(&pid) else {
            return false;
        };
        task.state = if succeeded {
            WatchTaskState::Succeeded
        } else {
            WatchTaskState::Failed
        };
        true
    }

    pub fn collect_removals(&mut self) -> Vec<u32> {
        let removable: Vec<u32> = self
            .tasks
            .iter()
            .filter_map(|(&pid, task)| {
                if !task.present && task.state != WatchTaskState::Running {
                    Some(pid)
                } else {
                    None
                }
            })
            .collect();

        for pid in &removable {
            self.tasks.remove(pid);
        }

        removable
    }

    pub fn should_exit_idle(&self, now_tick: u64) -> bool {
        if self.idle_exit_seconds == 0 {
            return false;
        }
        let idle_ms = u64::from(self.idle_exit_seconds) * 1000;
        now_tick.saturating_sub(self.last_new_tick) >= idle_ms
    }

    pub fn task_count(&self) -> usize {
        self.tasks.len()
    }
}

pub fn build_runtime_plan_from_text(
    text: &str,
    base_dir: &str,
) -> Option<(InjectorRuntimePlan, InjectorConfigInterop)> {
    let plan = InjectorRuntimePlan::from_ini_text(text, base_dir);
    let interop = InjectorConfigInterop::from_config(&plan.config)?;
    Some((plan, interop))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn runtime_plan_normalizes_paths_and_keeps_contracts() {
        let text = "[injector]\nprocess_name=DNF\ndll_path=game-payload.dll\noutput_dir=logs\n";
        let (plan, interop) =
            build_runtime_plan_from_text(text, "E:\\game-all\\artifacts\\run").expect("plan");

        assert_eq!(plan.config.process_name, "DNF.exe");
        assert_eq!(
            plan.config.dll_path,
            "E:\\game-all\\artifacts\\run\\game-payload.dll"
        );
        assert_eq!(plan.config.output_dir, "E:\\game-all\\artifacts\\run\\logs");
        assert_eq!(plan.helper_status_version, 5);
        assert_eq!(plan.helper_status_size, 152);
        assert_eq!(interop.process_name[0..7], *b"DNF.exe");
    }

    #[test]
    fn watch_runtime_tracks_pending_running_and_removal() {
        let config = InjectorConfig::default();
        let mut runtime = InjectorWatchRuntime::new(&config, 1000);

        runtime.observe_processes(1200, &[11, 22]);
        assert_eq!(runtime.collect_pending(8), vec![11, 22]);

        assert!(runtime.mark_started(11));
        assert!(runtime.mark_finished(11, true));

        runtime.observe_processes(1500, &[22]);
        assert_eq!(runtime.collect_removals(), vec![11]);
        assert_eq!(runtime.collect_pending(8), vec![22]);
    }

    #[test]
    fn watch_runtime_keeps_running_task_until_finished() {
        let config = InjectorConfig::default();
        let mut runtime = InjectorWatchRuntime::new(&config, 1000);

        runtime.observe_processes(1100, &[33]);
        assert!(runtime.mark_started(33));
        runtime.observe_processes(2000, &[]);
        assert!(runtime.collect_removals().is_empty());

        assert!(runtime.mark_finished(33, false));
        assert_eq!(runtime.collect_removals(), vec![33]);
    }

    #[test]
    fn watch_runtime_respects_idle_exit() {
        let config = InjectorConfig {
            idle_exit_seconds: 3,
            ..InjectorConfig::default()
        };
        let mut runtime = InjectorWatchRuntime::new(&config, 1000);
        runtime.observe_processes(2000, &[7]);
        assert!(!runtime.should_exit_idle(4000));
        assert!(runtime.should_exit_idle(5000));
    }

    #[test]
    fn helper_heartbeat_requires_contract_and_fresh_tick() {
        let ok = evaluate_helper_heartbeat(5, 152, true, 1000, 1200, 500);
        assert!(ok.contract_ok);
        assert!(ok.heartbeat_ok);

        let stale = evaluate_helper_heartbeat(5, 152, true, 1000, 2000, 500);
        assert!(!stale.heartbeat_ok);

        let mismatch = evaluate_helper_heartbeat(4, 152, true, 1000, 1200, 500);
        assert!(!mismatch.contract_ok);
        assert!(!mismatch.heartbeat_ok);
    }

    #[test]
    fn retry_runtime_retries_only_after_queued_failure() {
        let mut runtime = InjectionRetryRuntime::new(3, 1000);
        assert!(runtime.can_attempt());
        assert_eq!(runtime.current_attempt(), 1);

        let first = runtime.finish_attempt(true, false, false);
        assert_eq!(first.attempt, 1);
        assert!(first.backend_started);
        assert!(first.should_retry);
        assert_eq!(first.retry_delay_ms, 1000);
        assert!(first.used_heartbeat_fallback);
        assert!(!first.finished);
        assert_eq!(runtime.current_attempt(), 2);

        let second = runtime.finish_attempt(true, true, false);
        assert!(second.succeeded);
        assert_eq!(second.success_source, 1);
        assert!(!second.should_retry);
        assert!(second.finished);
        assert!(!runtime.can_attempt());
    }

    #[test]
    fn retry_runtime_stops_when_apc_never_queued() {
        let mut runtime = InjectionRetryRuntime::new(3, 1000);
        let result = runtime.finish_attempt(false, false, false);
        assert!(!result.backend_started);
        assert!(!result.succeeded);
        assert!(!result.should_retry);
        assert!(result.finished);
        assert!(!runtime.can_attempt());
    }

    #[test]
    fn finish_attempt_with_structured_results_uses_success_sources() {
        let mut runtime = InjectionRetryRuntime::new(3, 1000);
        let decision = finish_attempt_with_observation(
            &mut runtime,
            BackendExecutionResult {
                started: true,
                configured_backend: 2,
                effective_backend: 1,
                downgraded: true,
                error_code: 0,
            },
            SuccessObservationResult {
                observed: false,
                timed_out: true,
                used_fallback: false,
                error_code: 0,
            },
            HeartbeatObservationResult {
                observed: true,
                contract_ok: true,
                mapping_found: true,
                timed_out: false,
                error_code: 0,
            },
        );
        assert!(decision.succeeded);
        assert_eq!(decision.success_source, 2);
        assert!(decision.used_heartbeat_fallback);
        assert!(!decision.should_retry);
    }
}
