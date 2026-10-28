#!/bin/bash
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "Starting audit prototype..."
echo "Use bpftrace: bpftrace $ROOT/audit/audit.bt"
echo "Or kprobe: python3 $ROOT/audit/kprobe_audit.py"
