#!/bin/sh
set -u

echo "=== RingGuard boot verification ==="
echo "uname: $(uname -a)"
echo "cmdline: $(cat /proc/cmdline)"
echo "LSM: $(cat /sys/kernel/security/lsm 2>/dev/null || echo unavailable)"
echo "unprivileged_bpf_disabled: $(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || echo unavailable)"
if [ -e /sys/kernel/btf/vmlinux ]; then
    echo "BTF: present $(wc -c < /sys/kernel/btf/vmlinux) bytes"
else
    echo "BTF: missing"
fi
for opt in IO_URING BPF_SYSCALL BPF_LSM SECURITY DEBUG_INFO_BTF NET_9P_VIRTIO 9P_FS; do
    if zcat /proc/config.gz 2>/dev/null | grep -q "^CONFIG_$opt=y"; then
        echo "CONFIG_$opt=y"
    else
        echo "CONFIG_$opt=missing"
    fi
done

if [ -x /mnt/harness ]; then
    echo "=== io_uring NOP baseline ==="
    /mnt/harness 10000 64 5 || echo "HARNESS_FAILED"
fi

echo "=== Phase 3 verification ==="
if [ -x /mnt/loader ] && [ -x /mnt/deny_test ]; then
    /mnt/loader >/tmp/allow.log 2>&1 &
    LPID=$!
    sleep 1
    if grep -q "loaded and attached" /tmp/allow.log; then
        echo "ATTACH_PASS"
    else
        echo "ATTACH_FAIL"
        cat /tmp/allow.log
    fi
    kill "$LPID" 2>/dev/null || true
    wait "$LPID" 2>/dev/null || true

    /mnt/loader --block-opcode 0 >/tmp/deny.log 2>&1 &
    LPID=$!
    sleep 1
    OUT=$(/mnt/deny_test 200)
    echo "$OUT"
    case "$OUT" in
        *"ok=0 eperm=200 other=0"*) echo "DENY_PASS" ;;
        *) echo "DENY_FAIL" ;;
    esac
    grep -m2 'DENY' /tmp/deny.log || true

    kill "$LPID" 2>/dev/null || true
    wait "$LPID" 2>/dev/null || true
    sleep 1

    OUT=$(/mnt/deny_test 200)
    echo "$OUT"
    case "$OUT" in
        *"ok=200 eperm=0 other=0"*) echo "DETACH_PASS" ;;
        *) echo "DETACH_FAIL" ;;
    esac
else
    echo "PHASE3_SKIP (missing /mnt/loader or /mnt/deny_test)"
fi

echo "=== Phase 4 policy matrix ==="
p4_check() {
    label=$1
    expect=$2
    out=$3
    case "$out" in
        *"$expect"*) echo "P4_PASS $label" ;;
        *) echo "P4_FAIL $label (got: $out)" ;;
    esac
}

p4_start() {
    /mnt/loader "$@" >/tmp/p4_loader.log 2>&1 &
    P4_PID=$!
    sleep 1
    if grep -q "loaded and attached" /tmp/p4_loader.log; then
        echo "P4_ATTACH_PASS"
    else
        echo "P4_ATTACH_FAIL"
        cat /tmp/p4_loader.log
    fi
}

p4_stop() {
    kill "$P4_PID" 2>/dev/null || true
    wait "$P4_PID" 2>/dev/null || true
    sleep 1
}

if [ -x /mnt/policy_op ]; then
    p4_start
    out=$(/mnt/policy_op nop 0 100); p4_check "P0_nop_allow" "ok=100 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op read 64 100); p4_check "P0_read_allow" "ok=100 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op write 64 100); p4_check "P0_write_allow" "ok=100 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op provide 64 100); p4_check "P0_provide_allow" "ok=100 eperm=0 other=0" "$out"
    p4_stop

    p4_start --block-opcode 23
    out=$(/mnt/policy_op nop 0 100); p4_check "P1_nop_allow" "ok=100 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op write 64 100); p4_check "P1_write_deny" "ok=0 eperm=100 other=0" "$out"
    out=$(/mnt/policy_op read 64 100); p4_check "P1_read_allow" "ok=100 eperm=0 other=0" "$out"
    p4_stop

    p4_start --max-len 64
    out=$(/mnt/policy_op read 32 100); p4_check "P2_read_under" "ok=100 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op read 64 100); p4_check "P2_read_boundary" "ok=100 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op read 128 100); p4_check "P2_read_over" "ok=0 eperm=100 other=0" "$out"
    out=$(/mnt/policy_op write 128 100); p4_check "P4_write_over" "ok=0 eperm=100 other=0" "$out"
    out=$(/mnt/policy_op write 64 100); p4_check "P4_write_boundary" "ok=100 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op provide 32 50); p4_check "P4_provide_under" "ok=50 eperm=0 other=0" "$out"
    out=$(/mnt/policy_op provide 128 50); p4_check "P4_provide_over" "ok=0 eperm=50 other=0" "$out"
    out=$(/mnt/policy_op nop 0 100); p4_check "P4_nop_unaffected" "ok=100 eperm=0 other=0" "$out"
    p4_stop

    p4_start --deny-link-timeout
    out=$(/mnt/policy_op link_timeout 0 50); p4_check "P3_link_timeout_deny" "ok=0 eperm=50 other=0" "$out"
    out=$(/mnt/policy_op nop 0 100); p4_check "P3_nop_allow" "ok=100 eperm=0 other=0" "$out"
    p4_stop
else
    echo "PHASE4_SKIP (missing /mnt/policy_op)"
fi

echo "=== Phase 5 performance ==="
p5_run() {
    label=$1
    /mnt/harness 50000 64 10 >"/tmp/p5_$label.csv" 2>&1 || echo "P5_${label}_FAILED"
    echo "P5_${label}_BEGIN"
    cat "/tmp/p5_$label.csv"
    echo "P5_${label}_END"
}

if [ -x /mnt/harness ] && [ -x /mnt/loader ]; then
    /mnt/harness 50000 64 2 >/tmp/p5_base_warm.csv 2>&1 || echo "P5_WARMUP_FAILED"
    p5_run BASELINE

    /mnt/loader --quiet >/tmp/p5_loader_log.log 2>&1 &
    P5_PID=$!
    sleep 1
    if grep -q "loaded and attached" /tmp/p5_loader_log.log; then
        echo "P5_ATTACH_LOG_PASS"
        /mnt/harness 50000 64 5 >/tmp/p5_log_warm.csv 2>&1 || echo "P5_LOG_WARMUP_FAILED"
        p5_run GUARDED_LOG
    else
        echo "P5_ATTACH_LOG_FAIL"
        cat /tmp/p5_loader_log.log
    fi
    kill "$P5_PID" 2>/dev/null || true
    wait "$P5_PID" 2>/dev/null || true
    cat /tmp/p5_loader_log.log

    /mnt/harness 50000 64 2 >/tmp/p5_base_warm2.csv 2>&1 || echo "P5_WARMUP2_FAILED"
    p5_run BASELINE_WARM

    /mnt/loader --quiet --no-log --block-opcode 0 >/tmp/p5_nolog_deny.log 2>&1 &
    P5_PID=$!
    sleep 1
    if grep -q "loaded and attached" /tmp/p5_nolog_deny.log; then
        OUT=$(/mnt/deny_test 50)
        echo "$OUT"
        case "$OUT" in
            *"ok=0 eperm=50 other=0"*) echo "P5_NOLOG_ENFORCE_PASS" ;;
            *) echo "P5_NOLOG_ENFORCE_FAIL" ;;
        esac
    else
        echo "P5_NOLOG_ENFORCE_ATTACH_FAIL"
        cat /tmp/p5_nolog_deny.log
    fi
    kill "$P5_PID" 2>/dev/null || true
    wait "$P5_PID" 2>/dev/null || true

    /mnt/loader --quiet --no-log >/tmp/p5_loader_nolog.log 2>&1 &
    P5_PID=$!
    sleep 1
    if grep -q "loaded and attached" /tmp/p5_loader_nolog.log; then
        echo "P5_ATTACH_NOLOG_PASS"
        /mnt/harness 50000 64 2 >/tmp/p5_nolog_warm.csv 2>&1 || echo "P5_NOLOG_WARMUP_FAILED"
        p5_run GUARDED_NOLOG
    else
        echo "P5_ATTACH_NOLOG_FAIL"
        cat /tmp/p5_loader_nolog.log
    fi
    kill "$P5_PID" 2>/dev/null || true
    wait "$P5_PID" 2>/dev/null || true
    cat /tmp/p5_loader_nolog.log
else
    echo "PHASE5_SKIP (missing /mnt/harness or /mnt/loader)"
fi
echo "=== Phase 5 complete ==="

echo "=== verification complete ==="
