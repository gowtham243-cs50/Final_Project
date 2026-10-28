#!/usr/bin/env python3
from bcc import BPF
import ctypes as ct
import sys

b = BPF(src_file='/home/gowtham-r-nair/final_prject/ringguard-mvp/audit/kprobe_audit.c')
b.attach_kprobe(event="io_submit_sqe", fn_name="trace_io_submit_sqe")

class Event(ct.Structure):
    _fields_ = [
        ("pid", ct.c_uint),
        ("opcode", ct.c_ubyte),
        ("flags", ct.c_ubyte),
        ("fd", ct.c_int),
        ("len", ct.c_uint),
        ("off", ct.c_ulonglong),
        ("addr", ct.c_ulonglong),
        ("user_data", ct.c_ulonglong),
    ]

opcode_names = {0:"NOP",22:"READ",23:"WRITE",11:"TIMEOUT",15:"LINK_TIMEOUT",31:"PROVIDE_BUFFERS",32:"REMOVE_BUFFERS",18:"OPENAT",19:"CLOSE",4:"READ_FIXED",5:"WRITE_FIXED"}

def print_event(cpu, data, size):
    e = ct.cast(data, ct.POINTER(Event)).contents
    op = opcode_names.get(e.opcode, str(e.opcode))
    print(f"{e.pid},{op}({e.opcode}),fd={e.fd},len={e.len},off={e.off},addr={e.addr:#x},ud={e.user_data:#x}")

b["events"].open_perf_buffer(print_event)
print("pid,opcode,fd,len,off,addr,user_data")
try:
    while True:
        b.perf_buffer_poll()
except KeyboardInterrupt:
    pass
