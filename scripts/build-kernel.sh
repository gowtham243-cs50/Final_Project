#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KVER=${KVER:-5.15.223}
SRC="$ROOT/linux-$KVER"
JOBS=${JOBS:-6}
TOOLS="$ROOT/tools/root"

if [ -x "$TOOLS/usr/bin/flex" ]; then
    export PATH="$TOOLS/usr/bin:$PATH"
    export BISON_PKGDATADIR="$TOOLS/usr/share/bison"
    export M4="$TOOLS/usr/bin/m4"
fi

if [ ! -d "$SRC" ]; then
    echo "missing kernel source: $SRC" >&2
    exit 1
fi

cd "$SRC"

make defconfig

"$SRC/scripts/config" \
    --enable IO_URING \
    --enable BPF_SYSCALL \
    --enable BPF_JIT \
    --enable BPF_EVENTS \
    --enable BPF_LSM \
    --enable SECURITY \
    --enable SECURITYFS \
    --enable SECURITY_YAMA \
    --enable SECURITY_LOCKDOWN_LSM \
    --enable DEBUG_INFO \
    --enable DEBUG_INFO_BTF \
    --enable DEBUG_FS \
    --enable KPROBES \
    --enable FTRACE \
    --enable FUNCTION_TRACER \
    --enable NET \
    --enable NET_9P \
    --enable NET_9P_VIRTIO \
    --enable 9P_FS \
    --enable VIRTIO \
    --enable VIRTIO_PCI \
    --enable PCI \
    --enable TMPFS \
    --enable DEVTMPFS \
    --enable DEVTMPFS_MOUNT \
    --enable IKCONFIG \
    --enable IKCONFIG_PROC \
    --enable BLK_DEV_INITRD \
    --enable RD_GZIP \
    --enable KALLSYMS \
    --enable SERIAL_8250 \
    --enable SERIAL_8250_CONSOLE \
    --enable PROC_FS \
    --enable SYSFS \
    --set-str LSM "bpf"

make olddefconfig

make -j"$JOBS"

echo "kernel built: $SRC/arch/x86/boot/bzImage"
