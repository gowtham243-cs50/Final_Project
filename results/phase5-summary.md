# Phase 5 - Performance Results

- Kernel: `5.15.223 (gowtham-r-nair@gowtham-r-nair-Victus-by-HP-Laptop-16-e0xxx) (gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, GNU ld (GNU Binutils for Ubuntu) 2.42) #3 SMP Mon Oct 26 15:10:16 IST 2026`
- Workload: NOP requests, 50,000 per repetition, batch size 64, 10 measured reps after warm-up
- Baseline: no BPF policy attached
- Guarded logging: allow-all policy attached, every decision emitted to the BPF ring buffer and drained by the loader
- Guarded no-logging: allow-all policy attached, ring-buffer logging disabled (diagnostic run)

| Configuration | Reps | Mean ns/req | Median ns/req | Min | Max | Stddev | vs reference |
|---|---:|---:|---:|---:|---:|---:|---:|
| Baseline (no policy, cold) | 10 | 194.09 | 192.34 | 189.13 | 204.02 | 4.71 | 3.32x |
| Baseline (no policy, warm) | 10 | 58.54 | 58.34 | 56.74 | 59.96 | 0.87 | 1.00x |
| Guarded (policy + ring-buffer logging) | 10 | 17589.96 | 17598.74 | 17246.21 | 17882.39 | 172.37 | 300.50x |
| Guarded (policy only, no logging) | 10 | 422.88 | 422.11 | 421.10 | 426.47 | 1.91 | 7.22x |

Reference for ratios: Baseline (no policy, warm) (58.54 ns/req).

## Overhead

- Enforcement-only overhead (no logging): +364.34 ns/req (7.22x reference)
- Full guard overhead (policy + logging): +17531.43 ns/req (300.50x reference)
- Ring-buffer logging adds 17167.08 ns/req on top of enforcement
- Ratio sensitivity: full guard is 300.50x warm vs 90.63x cold baseline; policy-only is 7.22x warm vs 2.18x cold

## Logging volume

- Guarded logging loader drained 750,000 events
- Guarded no-logging loader drained 0 events

## Notes

- NOP is a microbenchmark: baseline per-request work is very small, so fixed per-request
  guard costs appear as a large ratio. Absolute ns/req is the more stable metric.
- The cold baseline runs immediately after boot at a low CPU frequency; the warm
  baseline is measured after load and is the steady-state reference.
- The logging run includes userspace draining of one ring-buffer event per request.
  Batching or sampling policy events is the obvious v2 optimization.
- Raw per-repetition data: `results/phase5.csv`; raw console log: `results/phase5-run.log`.
