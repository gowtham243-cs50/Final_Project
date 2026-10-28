#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/initramfs"
OUT="$ROOT/build/initramfs.cpio.gz"
BUSYBOX=${BUSYBOX:-/usr/bin/busybox}

rm -rf "$BUILD"
mkdir -p "$BUILD/bin"
mkdir -p "$BUILD/sbin"
mkdir -p "$BUILD/etc"
mkdir -p "$BUILD/proc"
mkdir -p "$BUILD/sys"
mkdir -p "$BUILD/dev"
mkdir -p "$BUILD/mnt"
mkdir -p "$BUILD/run"
mkdir -p "$BUILD/tmp"
mkdir -p "$BUILD/root"
cp "$BUSYBOX" "$BUILD/bin/busybox"
chmod 755 "$BUILD/bin/busybox"

for applet in sh mount umount mkdir cat ls echo dmesg poweroff reboot sleep grep sed awk ps free uname wc zcat; do
    ln -sf busybox "$BUILD/bin/$applet"
done

cat > "$BUILD/init" <<'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts 2>/dev/null || true
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true
mkdir -p /sys/kernel/security
mount -t securityfs securityfs /sys/kernel/security 2>/dev/null || true
mount -t tmpfs tmpfs /tmp
mount -t tmpfs tmpfs /run

echo "RINGGUARD_BOOT_OK"
echo "LSM: $(cat /sys/kernel/security/lsm 2>/dev/null || echo unavailable)"
echo "BTF: $(test -e /sys/kernel/btf/vmlinux && echo present || echo missing)"

if mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt 2>/dev/null; then
    echo "SHARE_MOUNTED"
    if [ -x /mnt/autorun.sh ]; then
        /mnt/autorun.sh
        echo "AUTORUN_DONE"
        poweroff -f
    fi
    exec /bin/sh
fi

echo "SHARE_MOUNT_FAILED"
exec /bin/sh
EOF
chmod 755 "$BUILD/init"

(cd "$BUILD" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip -9) > "$OUT"
echo "initramfs built: $OUT"
