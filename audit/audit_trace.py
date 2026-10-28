#!/usr/bin/env python3
from bcc import BPF
import ctypes as ct
import time
import sys

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
    u64 addr;
};

BPF_PERF_OUTPUT(events);

TRACEPOINT_PROBE(io_uring, io_uring_submit_sqe) {
    struct event e = {};
    e.ts = bpf_ktime_get_ns();
    u64 id = bpf_get_current_pid_tgid();
    e.pid = id >> 32;
    e.uid = bpf_get_current_uid_gid();
    
    // The tracepoint args vary by kernel version
    // Try to extract what we can
    e.opcode = args->opcode;
    e.flags = args->flags;
    e.fd = args->fd;
    e.user_data = args->user_data;
    // len/addr might be in different positions
    events.perf_submit(args, &e, sizeof(e));
    return 0;
}
"""

# Try to compile
try:
    b = BPF(text=bpf_text)
    print("attached", file=sys.stderr)
    
    class Event(ct.Structure):
        _fields_ = [
            ("ts", ct.c_ulonglong),
            ("pid", ct.c_uint),
            ("uid", ct.c_uint),
            ("opcode", ct.c_ubyte),
            ("flags", ct.c_ubyte),
            ("fd", ct.c_int),
            ("len", ct.c_uint),
            ("user_data", ct.c_ulonglong),
            ("addr", ct.c_ulonglong),
        ]
    
    block_opcodes = set([int(os.environ.get('BLOCK_OP', '-1'))])
    
    def print_event(cpu, data, size):
        event = ct.cast(data, ct.POINTER(Event)).contents
        opcode_names = {0:"NOP", 1:"READV", 2:"WRITEV", 3:"FSYNC", 4:"READ_FIXED", 5:"WRITE_FIXED", 6:"POLL_ADD", 7:"POLL_REMOVE", 8:"SYNC_FILE_RANGE", 9:"SENDMSG", 10:"RECVMSG", 11:"TIMEOUT", 12:"TIMEOUT_REMOVE", 13:"ACCEPT", 14:"ASYNC_CANCEL", 15:"LINK_TIMEOUT", 16:"CONNECT", 17:"FALLOCATE", 18:"OPENAT", 19:"CLOSE", 20:"FILES_UPDATE", 21:"STATX", 22:"READ", 23:"WRITE", 24:"FADVISE", 25:"MADVISE", 26:"SEND", 27:"RECV", 28:"OPENAT2", 29:"EPOLL_CTL", 30:"SPLICE", 31:"PROVIDE_BUFFERS", 32:"REMOVE_BUFFERS", 33:"TEE", 34:"SHUTDOWN", 35:"RENAMEAT", 36:"UNLINKAT", 37:"MKDIRAT", 38:"SYMLINKAT", 39:"LINKAT"}
        op = opcode_names.get(event.opcode, str(event.opcode))
        verdict = "DENY" if event.opcode in block_opcodes else "ALLOW"
        print(f"{event.ts},{event.pid},{event.uid},{op}({event.opcode}),{event.flags},{event.fd},{event.len},{event.user_data},{verdict}")
    
    b["events"].open_perf_buffer(print_event)
    print("ts,pid,uid,opcode,flags,fd,len,user_data,verdict")
    while True:
        b.perf_buffer_poll()
        
except Exception as e:
    print(f"ERR:{e}", file=sys.stderr)
    import traceback
    traceback.print_exc(file=sys.stderr)
    sys.exit(1)
