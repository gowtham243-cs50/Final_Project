SHELL := /bin/bash

ROOT := $(CURDIR)
KVER ?= 5.15.223
KERNEL := linux-$(KVER)/arch/x86/boot/bzImage
INITRD := build/initramfs.cpio.gz
DEMO_LOG := results/demo-run.log

.PHONY: all bpf initramfs kernel demo run summary check clean help

all: bpf initramfs

bpf:
	./scripts/build-bpf.sh

initramfs: bpf
	./scripts/build-initramfs.sh

kernel:
	./scripts/build-kernel.sh

demo: bpf initramfs
	@mkdir -p results
	@set -o pipefail; ./scripts/run-qemu.sh 2>&1 | tee $(DEMO_LOG)
	@python3 scripts/phase5-summary.py $(DEMO_LOG)
	@./scripts/demo-check.sh $(DEMO_LOG)

run: bpf initramfs
	./scripts/run-qemu.sh

summary:
	@python3 scripts/phase5-summary.py $(DEMO_LOG)

check:
	@./scripts/demo-check.sh $(DEMO_LOG)

clean:
	rm -rf build/initramfs build/initramfs.cpio.gz
	rm -f bpf/ringguard_policy.bpf.o bpf/ringguard_policy.skel.h
	rm -f share/loader share/loader_static share/deny_test share/policy_op share/harness

help:
	@echo "RingGuard MVP targets:"
	@echo "  make demo       build, boot QEMU, run all checks, summarize"
	@echo "  make run        build, boot QEMU (console output only)"
	@echo "  make summary    regenerate results/phase5-summary.md from $(DEMO_LOG)"
	@echo "  make check      re-run checks against $(DEMO_LOG)"
	@echo "  make bpf        build BPF object, skeleton, loader, and tests"
	@echo "  make initramfs  build the BusyBox initramfs"
	@echo "  make kernel     build linux-$(KVER) (slow)"
	@echo "  make clean      remove generated userspace and initramfs artifacts"
