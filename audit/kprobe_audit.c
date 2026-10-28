#include <uapi/linux/ptrace.h>
#include <linux/io_uring.h>

struct event {
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

int trace_io_submit_sqe(struct pt_regs *ctx) {
    struct event e = {};
    u64 id = bpf_get_current_pid_tgid();
    e.pid = id >> 32;
    
    const struct io_uring_sqe *sqe = (const struct io_uring_sqe *)PT_REGS_PARM3(ctx);
    if (!sqe) return 0;
    
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
