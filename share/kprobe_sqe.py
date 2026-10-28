#!/usr/bin/env python3
from bcc import BPF
import ctypes as ct

bpf_text = r"""
#include <uapi/linux/ptrace.h>
#include <linux/io_uring.h>

struct event {
    u64 ts;
    u32 pid;
    u8 opcode;
    u8 flags;
    s32 fd;
    u32 len;
    u64 off;
    u64 addr;
    u64 user_data;
};

BPF_PERF_OUTPUT(events);

int kprobe__io_submit_sqe(struct pt_regs *ctx) {
    struct event e = {};
    e.ts = bpf_ktime_get_ns();
    u64 id = bpf_get_current_pid_tgid();
    e.pid = id >> 32;
    
    // args: ctx, req, sqe
    struct io_uring_sqe *sqe = (struct io_uring_sqe *)PT_REGS_PARM3(ctx);
    bpf_probe_read_kernel(&e.opcode, sizeof(e.opcode), &sqe->opcode);
    bpf_probe_read_kernel(&e.flags, sizeof(e.flags), &sqe->flags);
    bpf_probe_read_kernel(&e.fd, sizeof(e.fd), &sqe->fd);
    bpf_probe_read_kernel(&e.len, sizeof(e.len), &sqe->len);
    bpf_probe_read_kernel(&e.off, sizeof(e.off), &sqe->off);
    bpf_probe_read_kernel(&e.addr, sizeof(e.addr), &sqe->addr);
    bpf_probe_read_kernel(&e.user_data, sizeof(e.user_data), &sqe->user_data);
    events.perf_submit(ctx, &e, sizeof(e));
    return 0;
}
"""

b = BPF(text=bpf_text)
class Event(ct.Structure):
    _fields_ = [
        ("ts", ct.c_ulonglong),
        ("pid", ct.c_uint),
        ("opcode", ct.c_ubyte),
        ("flags", ct.c_ubyte),
        ("fd", ct.c_int),
        ("len", ct.c_uint),
        ("off", ct.c_ulonglong),
        ("addr", ct.c_ulonglong),
        ("user_data", ct.c_ulonglong),
    ]

opcode_names = {0:"NOP",22:"READ",23:"WRITE",11:"TIMEOUT",15:"LINK_TIMEOUT",31:"PROVIDE_BUFFERS"}

def print_event(cpu, data, size):
    e = ct.cast(data, ct.POINTER(Event)).contents
    op = opcode_names.get(e.opcode, str(e.opcode))
    print(f"{e.pid},{op}({e.opcode}),fd={e.fd},len={e.len},off={e.off},ud={e.user_data:#x}")

b["events"].open_perf_buffer(print_event)
print("pid,opcode,fd,len,off,user_data")
try:
    while True:
        b.perf_buffer_poll()
except KeyboardInterrupt:
    pass
