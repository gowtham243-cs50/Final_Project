#!/bin/bash
set -e
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

echo "Generating vmlinux.h if needed..."
if [ ! -f bpf/vmlinux.h ]; then
    if [ -f linux-*/vmlinux ]; then
        bpftool btf dump file linux-*/vmlinux format c > bpf/vmlinux.h
    else
        echo "vmlinux not found" >&2
        exit 1
    fi
fi

echo "Compiling BPF program..."
clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -I. -Ibpf -I/usr/include -I/usr/include/x86_64-linux-gnu \
    -c bpf/ringguard_policy.bpf.c -o bpf/ringguard_policy.bpf.o

echo "Generating skeleton..."
bpftool gen skeleton bpf/ringguard_policy.bpf.o > bpf/ringguard_policy.skel.h

echo "Building harness..."
gcc -O2 -Wall -static -Isrc -o share/harness src/harness.c src/uring.c

echo "Building loader..."
gcc -O2 -Wall -static -I. -Ibpf -o share/loader src/loader.c -lbpf -lelf -lzstd -lz
cp share/loader share/loader_static

echo "Building deny test..."
gcc -O2 -Wall -static -Isrc -o share/deny_test tests/deny_nop.c src/uring.c

echo "Building policy matrix test..."
gcc -O2 -Wall -static -Isrc -o share/policy_op tests/policy_op.c src/uring.c

echo "Done: bpf/ringguard_policy.bpf.o, bpf/ringguard_policy.skel.h, share/harness, share/loader, share/deny_test, share/policy_op"
