#!/bin/sh
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG=${1:-$ROOT/results/demo-run.log}
fail=0

if [ ! -f "$LOG" ]; then
    echo "DEMO_FAIL missing log: $LOG" >&2
    exit 1
fi

check() {
    name=$1
    shift
    if "$@"; then
        printf 'DEMO_PASS %s\n' "$name"
    else
        printf 'DEMO_FAIL %s\n' "$name"
        fail=$((fail + 1))
    fi
}

have() {
    grep -q "$1" "$LOG"
}

count_is() {
    [ "$(grep -c "$1" "$LOG")" -eq "$2" ]
}

events_ok() {
    awk -F= '
        /^RingGuard events=/ {
            gsub(/\r/, "", $2)
            count[++n] = $2
        }
        END { exit !(n == 2 && count[1] > 0 && count[2] == 0) }
    ' "$LOG"
}

check "kernel_boot" have "RINGGUARD_BOOT_OK"
check "bpf_lsm_active" have "LSM: capability,lockdown,yama,bpf"
check "attach" have "ATTACH_PASS"
check "deny_nop" have "DENY_PASS"
check "deny_counts" have "count=200 ok=0 eperm=200 other=0"
check "detach" have "DETACH_PASS"
check "detach_counts" have "count=200 ok=200 eperm=0 other=0"
check "policy_cases_all_pass" count_is '^P4_PASS' 17
check "policy_cases_no_fail" count_is '^P4_FAIL' 0
check "benchmark_attach_log" have "P5_ATTACH_LOG_PASS"
check "benchmark_attach_nolog" have "P5_ATTACH_NOLOG_PASS"
check "no_log_still_enforces" have "P5_NOLOG_ENFORCE_PASS"
check "benchmark_baseline" have "P5_BASELINE_END"
check "benchmark_baseline_warm" have "P5_BASELINE_WARM_END"
check "benchmark_guarded_log" have "P5_GUARDED_LOG_END"
check "benchmark_guarded_nolog" have "P5_GUARDED_NOLOG_END"
check "logging_no_drops" events_ok
check "autorun_complete" have "AUTORUN_DONE"

if [ "$fail" -eq 0 ]; then
    echo "DEMO_RESULT PASS"
    exit 0
fi

echo "DEMO_RESULT FAIL ($fail checks failed)"
exit 1
