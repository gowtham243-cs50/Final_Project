#!/usr/bin/env python3
from bcc import BPF
import sys

bpf_text = """
#include <uapi/linux/ptrace.h>

struct data_t {
    u64 ctx;
    u64 req;
    u8 opcode;
    u64 user_data;
    u32 flags;
    u8 force_nonblock;
    u8 sq_thread;
};

BPF_PERF_OUTPUT(events);

TRACEPOINT_PROBE(io_uring, io_uring_submit_sqe) {
    struct data_t data = {};
    data.ctx = (u64)args->ctx;
    data.req = (u64)args->req;
    data.opcode = args->opcode;
    data.user_data = args->user_data;
    data.flags = args->flags;
    data.force_nonblock = args->force_nonblock;
    data.sq_thread = args->sq_thread;
    events.perf_submit(args, &data, sizeof(data));
    return 0;
}
"""

b = BPF(text=bpf_text)
import ctypes as ct

class Data(ct.Structure):
    _fields_ = [
        ("ctx", ct.c_ulonglong),
        ("req", ct.c_ulonglong),
        ("opcode", ct.c_ubyte),
        ("user_data", ct.c_ulonglong),
        ("flags", ct.c_uint),
        ("force_nonblock", ct.c_ubyte),
        ("sq_thread", ct.c_ubyte),
    ]

opcode_names = {0:"NOP", 1:"READV", 2:"WRITEV", 22:"READ", 23:"WRITE", 11:"TIMEOUT", 15:"LINK_TIMEOUT", 31:"PROVIDE_BUFFERS", 32:"REMOVE_BUFFERS", 18:"OPENAT", 19:"CLOSE"}

def print_event(cpu, data, size):
    e = ct.cast(data, ct.POINTER(Data)).contents
    op = opcode_names.get(e.opcode, str(e.opcode))
    print(f"{e.ctx},{e.req},{op}({e.opcode}),{e.user_data:#x},{e.flags},{e.force_nonblock},{e.sq_thread}")

b["events"].open_perf_buffer(print_event)
print("ctx,req,opcode,user_data,flags,force_nonblock,sq_thread")
try:
    while True:
        b.perf_buffer_poll()
except KeyboardInterrupt:
    pass
