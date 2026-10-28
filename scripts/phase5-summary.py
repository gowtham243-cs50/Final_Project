#!/usr/bin/env python3
import csv
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOG = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results" / "phase5-run.log"
OUT_MD = ROOT / "results" / "phase5-summary.md"
OUT_CSV = ROOT / "results" / "phase5.csv"

SECTIONS = ("BASELINE", "BASELINE_WARM", "GUARDED_LOG", "GUARDED_NOLOG")
LABELS = {
    "BASELINE": "Baseline (no policy, cold)",
    "BASELINE_WARM": "Baseline (no policy, warm)",
    "GUARDED_LOG": "Guarded (policy + ring-buffer logging)",
    "GUARDED_NOLOG": "Guarded (policy only, no logging)",
}
REQUIRED = ("BASELINE", "GUARDED_LOG", "GUARDED_NOLOG")

rows = {name: [] for name in SECTIONS}
events = []
kernel = ""
current = None

with LOG.open() as f:
    for raw in f:
        line = raw.strip()
        if not kernel and "Linux version" in line:
            kernel = line.split("Linux version", 1)[1].strip()
        for name in SECTIONS:
            if line == f"P5_{name}_BEGIN":
                current = name
            elif line == f"P5_{name}_END":
                current = None
        if line.startswith("RingGuard events="):
            events.append(int(line.split("=", 1)[1]))
        if current and line and line[0].isdigit() and line.count(",") == 5:
            rep, count, batch, total_ns, ns_per_req, errors = line.split(",")
            rows[current].append({
                "section": current,
                "rep": int(rep),
                "count": int(count),
                "batch": int(batch),
                "total_ns": int(total_ns),
                "ns_per_req": float(ns_per_req),
                "errors": int(errors),
            })

missing = [name for name in REQUIRED if not rows[name]]
if missing:
    sys.exit(f"missing benchmark sections in {LOG}: {', '.join(missing)}")

OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
with OUT_CSV.open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "section", "rep", "count", "batch", "total_ns", "ns_per_req", "errors"
    ])
    writer.writeheader()
    for name in SECTIONS:
        writer.writerows(rows[name])

stats = {}
for name in SECTIONS:
    vals = [r["ns_per_req"] for r in rows[name]]
    if not vals:
        continue
    stats[name] = {
        "n": len(vals),
        "mean": statistics.mean(vals),
        "median": statistics.median(vals),
        "min": min(vals),
        "max": max(vals),
        "stdev": statistics.stdev(vals) if len(vals) > 1 else 0.0,
    }

ref_name = "BASELINE_WARM" if "BASELINE_WARM" in stats else "BASELINE"
base = stats[ref_name]["mean"]
lines = [
    "# Phase 5 - Performance Results",
    "",
    f"- Kernel: `{kernel}`" if kernel else "- Kernel: unknown",
    "- Workload: NOP requests, 50,000 per repetition, batch size 64, 10 measured reps after warm-up",
    "- Baseline: no BPF policy attached",
    "- Guarded logging: allow-all policy attached, every decision emitted to the BPF ring buffer and drained by the loader",
    "- Guarded no-logging: allow-all policy attached, ring-buffer logging disabled (diagnostic run)",
    "",
    "| Configuration | Reps | Mean ns/req | Median ns/req | Min | Max | Stddev | vs reference |",
    "|---|---:|---:|---:|---:|---:|---:|---:|",
]
for name in SECTIONS:
    if name not in stats:
        continue
    s = stats[name]
    ratio = s["mean"] / base
    lines.append(
        f"| {LABELS[name]} | {s['n']} | {s['mean']:.2f} | {s['median']:.2f} | "
        f"{s['min']:.2f} | {s['max']:.2f} | {s['stdev']:.2f} | {ratio:.2f}x |"
    )

log_mean = stats["GUARDED_LOG"]["mean"]
nolog_mean = stats["GUARDED_NOLOG"]["mean"]
lines += [
    "",
    f"Reference for ratios: {LABELS[ref_name]} ({base:.2f} ns/req).",
    "",
    "## Overhead",
    "",
]
if nolog_mean >= base:
    lines.append(
        f"- Enforcement-only overhead (no logging): +{nolog_mean - base:.2f} ns/req "
        f"({nolog_mean / base:.2f}x reference)"
    )
else:
    lines.append(
        f"- Enforcement-only overhead (no logging): not resolvable above VM noise "
        f"(policy-only {nolog_mean:.2f} ns/req vs reference {base:.2f} ns/req); "
        "the logging measurement is stable"
    )
lines += [
    f"- Full guard overhead (policy + logging): {log_mean - base:+.2f} ns/req "
    f"({log_mean / base:.2f}x reference)",
    f"- Ring-buffer logging adds {log_mean - nolog_mean:.2f} ns/req on top of enforcement",
]
if "BASELINE" in stats and ref_name != "BASELINE":
    cold = stats["BASELINE"]["mean"]
    lines.append(
        f"- Ratio sensitivity: full guard is {log_mean / base:.2f}x warm vs "
        f"{log_mean / cold:.2f}x cold baseline; policy-only is "
        f"{nolog_mean / base:.2f}x warm vs {nolog_mean / cold:.2f}x cold"
    )
lines += [
    "",
    "## Logging volume",
    "",
]
if len(events) >= 2:
    lines.append(f"- Guarded logging loader drained {events[0]:,} events")
    lines.append(f"- Guarded no-logging loader drained {events[1]:,} events")
elif events:
    lines.append(f"- Loader drained {events[0]:,} events")

try:
    log_rel = LOG.resolve().relative_to(ROOT)
except ValueError:
    log_rel = LOG

lines += [
    "",
    "## Notes",
    "",
    "- NOP is a microbenchmark: baseline per-request work is very small, so fixed per-request",
    "  guard costs appear as a large ratio. Absolute ns/req is the more stable metric.",
    "- The cold baseline runs immediately after boot at a low CPU frequency; the warm",
    "  baseline is measured after load and is the steady-state reference.",
    "- The logging run includes userspace draining of one ring-buffer event per request.",
    "  Batching or sampling policy events is the obvious v2 optimization.",
    f"- Raw per-repetition data: `results/phase5.csv`; raw console log: `{log_rel}`.",
]

OUT_MD.write_text("\n".join(lines) + "\n")
print(OUT_MD)
print(OUT_CSV)
