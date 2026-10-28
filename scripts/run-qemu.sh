#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KVER=${KVER:-5.15.223}
KERNEL="$ROOT/linux-$KVER/arch/x86/boot/bzImage"
INITRD="$ROOT/build/initramfs.cpio.gz"
SHARE="$ROOT/share"

if [ ! -f "$KERNEL" ]; then
    echo "missing kernel: $KERNEL" >&2
    exit 1
fi
if [ ! -f "$INITRD" ]; then
    echo "missing initramfs: $INITRD" >&2
    exit 1
fi

KVM_ARGS=""
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    KVM_ARGS="-enable-kvm"
else
    echo "KVM unavailable; using TCG" >&2
fi

exec qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "console=ttyS0 rdinit=/init lsm=lockdown,yama,bpf" \
    -display none \
    -serial stdio \
    -no-reboot \
    -m 2048 \
    -smp 2 \
    -virtfs "local,path=$SHARE,mount_tag=hostshare,security_model=none" \
    $KVM_ARGS
