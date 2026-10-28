# RingGuard MVP — Full Implementation Plan (Original)

RingGuard MVP — Full Implementation Plan
0. Scope
MVP v1 (this plan): a working, kernel-enforced io_uring request guard.
- Add a kernel hook at io_uring submission time.
- Attach an eBPF policy program to the hook.
- Policy can allow or deny an SQE before its handler runs.
- Denied requests produce -EPERM CQEs and never execute.
- Decisions are logged to a BPF ring buffer.
- Policies cover opcode blocking, buffer-length validation, READ-length validation, and linked-timeout blocking.
- Baseline vs guarded performance is measured.
- Everything runs in a QEMU VM; the host kernel is untouched.
Not in v1: custom BPF_PROG_TYPE_RG, custom helpers, paper-equivalent verifier extensions, per-ring attachment, request modification, threshold/timeout batching. Those are the paper-faithful v2 stretch.

1. Architecture
User app
  │  fills SQEs
  ▼
io_uring submission queue
  │
  ▼
io_submit_sqes()
  ├─ io_get_sqe()
  ├─ copy SQE to kernel-owned ksqe
  ├─ io_submit_sqe()
  │    ├─ io_init_req()
  │    ├─ security_uring_submit_sqe(ctx, &ksqe)  <── BPF LSM policy
  │    │      ├─ allow  -> continue
  │    │      └─ deny   -> fail request with -EPERM
  │    └─ io_req_prep()
  ▼
normal io_uring handler
The SQE is copied once and the copy is used for both policy and parsing. This prevents a post-audit userspace mutation from changing the request after the verdict.

2. Environment plan
[As specified in original plan - 5.15 LTS, QEMU, BusyBox initramfs, 9p share, etc.]

3. Repository layout
ringguard-mvp/
├── README.md
├── Makefile
├── kernel/
├── bpf/
├── src/
├── scripts/
├── tests/
├── results/
└── REPORT.md

# PROGRESS - What's Been Done

## Phase 0: Environment and baseline [DONE]
- All packages installed (qemu, build tools, bpf tools, libbpf)
- Linux kernel 5.15.223 downloaded and built
- vmlinux.h generated
- BusyBox initramfs created with /init mounting 9p share
- QEMU boots successfully with patched kernel
- Verified: /sys/kernel/security/lsm contains capability,lockdown,yama,bpf; BTF present; CONFIG_BPF_LSM=y, CONFIG_IO_URING=y enabled
- io_uring NOP baseline harness works (harness in share/)

## Phase 1: Audit prototype [DONE]
Created in `audit/`:
- kprobe_audit.py - kprobe on io_submit_sqe to log opcode/fd/len/off/addr/user_data
- trace_submit.py - tracepoint probe for io_uring:io_uring_submit_sqe
- audit.bt - bpftrace script
- audit_policy.py - userspace policy engine (block opcode, max len, deny LINK_TIMEOUT)
- audit-run.sh helper
Copied to share/ for use in VM

## Phase 2: Kernel enforcement patch [DONE]
**Patches applied to linux-5.15.223:**

1. `include/linux/lsm_hook_defs.h` - Added uring_submit_sqe hook
```c
#ifdef CONFIG_IO_URING
struct io_uring_sqe;
LSM_HOOK(int, 0, uring_submit_sqe, void *ring_ctx, const struct io_uring_sqe *sqe)
#endif
```

2. `include/linux/security.h` - Added prototype
```c
struct io_uring_sqe;
extern int security_uring_submit_sqe(void *ring_ctx, const struct io_uring_sqe *sqe);
```

3. `security/security.c` - Added implementation
```c
#ifdef CONFIG_IO_URING
int security_uring_submit_sqe(void *ring_ctx, const struct io_uring_sqe *sqe)
{
	return call_int_hook(uring_submit_sqe, 0, ring_ctx, sqe);
}
#endif
```

4. `io_uring/io_uring.c` - Enforcement
- Added `#include <linux/security.h>`
- Changed `io_submit_sqe` to `static int io_submit_sqe(struct io_ring_ctx *ctx, struct io_kiocb *req, const struct io_uring_sqe *sqe, bool *denied)`
- In `io_init_req`, added `security_uring_submit_sqe(ctx, sqe)` check returning -EPERM if denied
- In `io_submit_sqes`, copy SQE to `ksqe` on stack, call policy, if denied continue loop (doesn't break); use `denied` flag to handle properly

Kernel rebuilt, boots in QEMU. Hook is present.

## Current State
- Working directory: `/home/gowtham-r-nair/final_prject/ringguard-mvp/`
- Patched kernel ready: `linux-5.15.223/arch/x86/boot/bzImage`
- Initramfs ready: `build/initramfs.cpio.gz`
- Can boot with `scripts/run-qemu.sh`
- Share contains harness, loader, deny test, and audit tools
- Phases 3, 4, and 5 complete and verified; next: optional Phase 6 or packaging

## Files Created/Modified
**New:** audit/*, share/* (copies), RINGGUARD_FULL_CONTEXT.md, PROGRESS.md  
**Modified:** kernel source files as above


## Phase 3: BPF policy program and loader [DONE]
Created:
- `bpf/common.h` - shared structs (ringguard_config, event)
- `bpf/ringguard_policy.bpf.c` - LSM BPF program attached to `lsm/uring_submit_sqe`, reads SQE fields, applies policy (block_opcode, max_len for PROVIDE_BUFFERS/READ/WRITE, deny_link_timeout), logs to ring buffer, returns verdict
- `bpf/ringguard_policy.skel.h` - generated skeleton
- `bpf/ringguard_policy.bpf.o` - compiled BPF object
- `src/loader.c` - userspace loader using libbpf, loads/attaches LSM program, configures maps, polls ring buffer
- `share/loader` - compiled loader binary
- `scripts/build-bpf.sh` - build script

Policy supports: P1 opcode blocking, P2/P4 length validation, P3 LINK_TIMEOUT blocking, P0 allow all.

Next up: optional Phase 6 (paper-faithful v2) or packaging. 

## Phase 3 - Verified Status
- Kernel #3 boots with the `uring_submit_sqe` LSM hook; `bpf_lsm_uring_submit_sqe` present in BTF/symbols.
- BPF policy, skeleton, static loader, and static deny test built in `share/`.
- Initramfs rebuilt from `scripts/build-initramfs.sh`; mounts securityfs and runs `share/autorun.sh`.
- Verified in QEMU (`results/phase3-run.log`):
  - `ATTACH_PASS` - policy attaches to `lsm/uring_submit_sqe`
  - `[DENY] ... opcode=NOP(0) ... verdict=-1` - ring buffer events
  - `DENY_TEST count=200 ok=0 eperm=200 other=0` / `DENY_PASS` - NOP denied with `-EPERM`
  - `DENY_TEST count=200 ok=200 eperm=0 other=0` / `DETACH_PASS` - detach restores allow
- Note: policy config now uses an explicit `block_enabled` flag because opcode 0 (NOP) cannot be encoded with a zero sentinel.
- Phase 4 policy matrix verified (`results/phase4-run.log`), 17/17 cases passed: allow-all, opcode block, READ/WRITE/PROVIDE_BUFFERS length boundaries, LINK_TIMEOUT deny, and unaffected-op checks.
- Phase 5 performance verified (`results/phase5-summary.md`): cold baseline ~193 ns/req, warm baseline ~59 ns/req, policy-only ~423 ns/req (+364 ns, 7.2x warm), policy+logging ~17.7 us/req (~300x warm, ~91x cold) on a NOP microbenchmark; logging dominates and `--no-log` still enforces.
- Next: optional Phase 6 (paper-faithful v2) or packaging (`README.md`, `REPORT.md`).

