# RingGuard MVP - Progress Report

## Environment
- Host: Linux
- Kernel source: linux-5.15.223 (patched)
- VM: QEMU with 9p share, KVM enabled
- BPF/LSM enabled, BTF present

## Phase 0 - Environment and baseline [DONE]
- Installed dependencies
- Fetched and built Linux 5.15.223
- Generated vmlinux.h
- Built BusyBox initramfs
- Booted QEMU VM successfully
- Verified: `LSM: capability,lockdown,yama,bpf`, `BTF: present`, `CONFIG_BPF_LSM=y`, `CONFIG_IO_URING=y`
- Harness runs and completes NOP baseline tests

## Phase 1 - Audit prototype [DONE]
Created audit tools:
- `audit/kprobe_audit.py` - kprobe on io_submit_sqe to log SQE fields
- `audit/trace_submit.py` - tracepoint attempt (io_uring_submit_sqe)
- `audit/audit.bt` - bpftrace script
- `audit/audit_policy.py` - policy engine for post-analysis
- `audit/audit-run.sh` - helper script
- `share/kprobe_audit.py`, `share/trace_submit.py` copied for in-VM use

## Phase 2 - Kernel enforcement patch [DONE]
Modified kernel (linux-5.15.223):
1. **include/linux/lsm_hook_defs.h** - Added `uring_submit_sqe` LSM hook (guarded by CONFIG_IO_URING)
2. **include/linux/security.h** - Added `struct io_uring_sqe` forward decl and `security_uring_submit_sqe()` prototype
3. **security/security.c** - Added `security_uring_submit_sqe()` implementation calling `uring_submit_sqe` hook
4. **io_uring/io_uring.c** - Added `#include <linux/security.h>`, modified `io_submit_sqe()` to take `bool *denied` param, added security check in `io_init_req()`, modified `io_submit_sqes()` to copy SQE to kernel stack and pass to policy hook; denied requests return -EPERM and loop continues

Kernel rebuilt successfully. bzImage at `linux-5.15.223/arch/x86/boot/bzImage`. Hook present in vmlinux/BTF.

## Phase 3 - BPF policy program and loader [DONE]
- `bpf/ringguard_policy.bpf.c` attaches to `lsm/uring_submit_sqe`; built with BTF (`-g`)
- `bpf/common.h` config uses an explicit `block_enabled` flag so opcode 0 (NOP) can be blocked
- `src/loader.c` loads/attaches the skeleton, configures the policy map, polls the ring buffer
- `src/uring.c`/`src/uring.h` export `rg_wait_cqes_status()` to inspect `cqe->res`
- `tests/deny_nop.c` submits NOP SQEs and counts ok/`-EPERM`/other completions
- `share/autorun.sh` runs attach, deny, and detach checks in the VM
- `scripts/build-bpf.sh` builds the BPF object, skeleton, static loader, and static deny test

Verified in QEMU on Linux 5.15.223 `#3` (`results/phase3-run.log`):
- `ATTACH_PASS` - loader attaches the LSM policy
- `[DENY] ... opcode=NOP(0) ... verdict=-1` - ring buffer decision events
- `DENY_TEST count=200 ok=0 eperm=200 other=0` / `DENY_PASS` - every NOP returned `-EPERM`
- `DENY_TEST count=200 ok=200 eperm=0 other=0` / `DETACH_PASS` - requests allowed after detach

## Phase 4 - Policies and test matrix [DONE]
- Added `tests/policy_op.c`: submits NOP, READ, WRITE, PROVIDE_BUFFERS, or LINK_TIMEOUT and counts CQE results
- Added policy matrix to `share/autorun.sh`; each policy is attached fresh via the loader
- Verified in QEMU (`results/phase4-run.log`), 17/17 cases passed:
  - P0 allow-all: NOP, READ, WRITE, PROVIDE_BUFFERS all succeed
  - P1 opcode block: WRITE gets `-EPERM`; NOP and READ still succeed
  - P2 READ length: 32 and 64 succeed (boundary), 128 gets `-EPERM`
  - P4 WRITE length: 64 succeeds, 128 gets `-EPERM`
  - P4 PROVIDE_BUFFERS length: 32 succeeds, 128 gets `-EPERM`
  - P3 LINK_TIMEOUT: gets `-EPERM`; NOP still succeeds
  - NOP remains unaffected by length policies

## Phase 5 - Performance evaluation [DONE]
- Added quiet drain and `--no-log` modes to the loader for benchmarking
- Added cold baseline, warm baseline, guarded-logging, and guarded-no-logging runs to `share/autorun.sh`
- Added a `--no-log` enforcement check (`P5_NOLOG_ENFORCE_PASS`) to prove logging can be disabled without disabling policy
- Added `scripts/phase5-summary.py`; outputs in `results/phase5-summary.md`, `results/phase5.csv`, `results/demo-run.log`
- NOP microbenchmark (50,000 req/rep, batch 64, 10 reps after warm-up, kernel #3; stable across 3 runs):
  - Cold baseline (immediately after boot): ~193 ns/req
  - Warm baseline (steady state): ~59 ns/req
  - Guarded policy only (no logging): ~423 ns/req (+364 ns/req over warm baseline, 7.2x)
  - Guarded policy + ring-buffer logging: ~17.7 us/req (~300x warm baseline, ~91x cold baseline)
  - Ring-buffer logging adds ~17.3 us/req; 750,000/750,000 events drained with no drops
- Interpretation: the enforcement hook costs ~364 ns/req in steady state; per-event logging dominates, so batching/sampling is the main v2 optimization
- Note: ratios are sensitive to host CPU frequency scaling; absolute per-request latencies are the stable metric

## Phase 6 - Paper-faithful v2 (optional) [PENDING]

## Current State
- Phases 3, 4, and 5 complete and verified end-to-end in QEMU
- Phase 4: 17/17 policy cases pass; Phase 5: overhead characterized and recorded
- Demo is reproducible with `make demo`; see `DEMO.md`
- Next: optional Phase 6 (paper-faithful v2) or package `README.md` / `REPORT.md`

## Demonstration
- `make demo` builds userspace artifacts and initramfs, boots QEMU, runs Phases 3-5,
  regenerates `results/phase5-summary.md`, and asserts `DEMO_RESULT PASS`
- `make check` re-runs the pass/fail checks against `results/demo-run.log`
- `make summary` regenerates the Phase 5 summary from the demo log
- Full walkthrough and evidence table: `DEMO.md`
