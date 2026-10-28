#!/usr/bin/env python3
from bcc import BPF
import ctypes as ct
import time
import sys
import os

bpf_text = r"""
#include <uapi/linux/ptrace.h>
#include <linux/io_uring.h>

struct event {
    u64 ts;
    u32 pid;
    u32 uid;
    u8 opcode;
    u8 flags;
    s32 fd;
    u32 len;
    u64 user_data;
};

BPF_RINGBUF_OUTPUT(events, 256);

TRACEPOINT_PROBE(io_uring, io_uring_submit_sqe) {
    struct event e = {};
    e.ts = bpf_ktime_get_ns();
    u64 id = bpf_get_current_pid_tgid();
    e.pid = id >> 32;
    e.uid = bpf_get_current_uid_gid();
    
    // Try to read from args if available
    // For different kernel versions, field names vary
    return 0;
}
"""

# Simpler approach - use kprobe on io_submit_sqe
bpf_text = r"""
#include <uapi/linux/ptrace.h>
#include <linux/io_uring.h>

struct event {
    u64 ts;
    u32 pid;
    u32 uid;
    u8 opcode;
    u8 flags;
    s32 fd;
    u32 len;
    u64 user_data;
};

BPF_PERF_OUTPUT(events);

int trace_io_submit_sqe(struct pt_regs *ctx) {
    struct event e = {};
    e.ts = bpf_ktime_get_ns();
    u64 id = bpf_get_current_pid_tgid();
    e.pid = id >> 32;
    e.uid = bpf_get_current_uid_gid();
    
    // Try to read the ksqe/sqe from args - depends on kernel version
    // In general, 3rd arg might be sqe in some versions
    struct io_uring_sqe *sqe = NULL;
    // This is a rough attempt; might need adjustment
    bpf_probe_read_kernel(&e.opcode, sizeof(e.opcode), &sqe->opcode);
    bpf_probe_read_kernel(&e.flags, sizeof(e.flags), &sqe->flags);
    bpf_probe_read_kernel(&e.fd, sizeof(e.fd), &sqe->fd);
    bpf_probe_read_kernel(&e.len, sizeof(e.len), &sqe->len);
    bpf_probe_read_kernel(&e.user_data, sizeof(e.user_data), &sqe->user_data);
    
    events.perf_submit(ctx, &e, sizeof(e));
    return 0;
}
"""
