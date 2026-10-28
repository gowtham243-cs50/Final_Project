#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "common.h"

#ifndef EPERM
#define EPERM 1
#endif

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct ringguard_config);
} cfg SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} events SEC(".maps");

SEC("lsm/uring_submit_sqe")
int BPF_PROG(uring_submit_sqe, void *ring_ctx, const struct io_uring_sqe *sqe)
{
    __u32 key = 0;
    struct ringguard_config *c = bpf_map_lookup_elem(&cfg, &key);
    __u8 opcode = 0, flags = 0;
    __s32 fd = 0;
    __u32 len = 0;
    __u64 user_data = 0;
    int verdict = 0;

    bpf_probe_read_kernel(&opcode, sizeof(opcode), &sqe->opcode);
    bpf_probe_read_kernel(&flags, sizeof(flags), &sqe->flags);
    bpf_probe_read_kernel(&fd, sizeof(fd), &sqe->fd);
    bpf_probe_read_kernel(&len, sizeof(len), &sqe->len);
    bpf_probe_read_kernel(&user_data, sizeof(user_data), &sqe->user_data);

    if (c) {
        if (c->block_enabled && c->block_opcode == opcode)
            verdict = -EPERM;
        if (c->max_len && opcode == IORING_OP_PROVIDE_BUFFERS && len > c->max_len)
            verdict = -EPERM;
        if (c->max_len && (opcode == IORING_OP_READ || opcode == IORING_OP_WRITE) && len > c->max_len)
            verdict = -EPERM;
        if (c->deny_link_timeout && opcode == IORING_OP_LINK_TIMEOUT)
            verdict = -EPERM;
    }

    if (c && c->log_enabled) {
        struct event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);

        if (e) {
            e->ts = bpf_ktime_get_ns();
            e->pid = bpf_get_current_pid_tgid() >> 32;
            e->uid = bpf_get_current_uid_gid();
            e->ring_ctx = (__u64)ring_ctx;
            e->opcode = opcode;
            e->flags = flags;
            e->fd = fd;
            e->len = len;
            e->user_data = user_data;
            e->verdict = verdict;
            bpf_ringbuf_submit(e, 0);
        }
    }

    return verdict;
}

char LICENSE[] SEC("license") = "GPL";
