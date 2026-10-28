# RingGuard MVP Demo

One command builds the userspace artifacts, boots the patched kernel in QEMU,
runs every verification phase, writes a summary, and asserts PASS/FAIL.

```bash
cd ~/final_prject/ringguard-mvp
make demo
```

The run takes roughly 30-40 seconds. QEMU exits and powers off by itself.

## What happens

1. `scripts/build-bpf.sh` builds the BPF policy, skeleton, loader, harness, and tests.
2. `scripts/build-initramfs.sh` builds a BusyBox initramfs.
3. QEMU boots `linux-5.15.223` (kernel `#3`) with the 9p share mounted at `/mnt`.
4. `share/autorun.sh` runs the three verification phases.
5. `scripts/phase5-summary.py` writes `results/phase5-summary.md` and `results/phase5.csv`.
6. `scripts/demo-check.sh` checks the console log and prints `DEMO_RESULT PASS`.

Full console log: `results/demo-run.log`

## Expected highlights

```text
Linux version 5.15.223 ... #3 SMP
LSM: capability,lockdown,yama,bpf
BTF: present
CONFIG_BPF_LSM=y
CONFIG_IO_URING=y

=== Phase 3 verification ===
ATTACH_PASS
DENY_TEST count=200 ok=0 eperm=200 other=0
DENY_PASS
DENY_TEST count=200 ok=200 eperm=0 other=0
DETACH_PASS

=== Phase 4 policy matrix ===
... 17 lines of P4_PASS ..., no P4_FAIL

=== Phase 5 performance ===
P5_ATTACH_LOG_PASS
P5_NOLOG_ENFORCE_PASS
P5_ATTACH_NOLOG_PASS
RingGuard events=750000
RingGuard events=0
=== Phase 5 complete ===
AUTORUN_DONE
```

Final assertion from `make demo`:

```text
DEMO_RESULT PASS
```

## What each check proves

| Evidence | Meaning |
|---|---|
| `LSM: ...bpf` and `CONFIG_BPF_LSM=y` | The custom LSM hook is active in the running kernel |
| `ATTACH_PASS` | The BPF policy program loaded and attached |
| `ok=0 eperm=200` | All 200 NOP requests were denied with `-EPERM` by the kernel |
| `[DENY] ... verdict=-1` | Denials are reported through the BPF ring buffer |
| `ok=200 eperm=0` after detach | The same workload succeeds without the policy, proving the denial came from RingGuard |
| 17 `P4_PASS`, 0 `P4_FAIL` | Opcode blocking, length limits, PROVIDE_BUFFERS, and LINK_TIMEOUT rules all behave correctly |
| `P5_NOLOG_ENFORCE_PASS` | Logging can be disabled without disabling enforcement |
| `RingGuard events=750000` | 750,000 policy events were drained with no drops |
| `phase5-summary.md` | Cold baseline ~193 ns/req, warm baseline ~59 ns/req, policy-only ~423 ns/req (+364 ns, 7.2x warm), policy+logging ~17.7 us/req (~300x warm, ~91x cold) |

The strongest live moment is the deny/detach pair: 200/200 requests are denied
with the guard attached, then 200/200 succeed immediately after detach.

## Show the generated report

```bash
cat results/phase5-summary.md
```

Regenerate the summary from the demo log:

```bash
make summary
```

## Re-check without rebooting

```bash
make check
```

## Manual equivalent

```bash
./scripts/build-bpf.sh
./scripts/build-initramfs.sh
./scripts/run-qemu.sh 2>&1 | tee results/demo-run.log
python3 scripts/phase5-summary.py results/demo-run.log
./scripts/demo-check.sh results/demo-run.log
```

## Troubleshooting

- **Missing kernel image:** `linux-5.15.223/arch/x86/boot/bzImage` must exist.
  Rebuild it with `make kernel` (slow), or restore the prebuilt tree.
- **KVM unavailable:** boot still works using TCG, but the demo is much slower.
  Check `ls -l /dev/kvm` and group membership with `id`.
- **Missing tools:** install the host packages listed in `requirements.txt`.
- **BusyBox path:** `scripts/build-initramfs.sh` defaults to `/usr/bin/busybox`;
  override with `BUSYBOX=/path/to/busybox make initramfs`.
