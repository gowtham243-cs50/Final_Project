#ifndef RINGGUARD_COMMON_H
#define RINGGUARD_COMMON_H

struct ringguard_config {
    __u32 block_opcode;
    __u32 block_enabled;
    __u32 max_len;
    __u32 deny_link_timeout;
    __u32 log_enabled;
};

struct event {
    __u64 ts;
    __u32 pid;
    __u32 uid;
    __u64 ring_ctx;
    __u8 opcode;
    __u8 flags;
    __s32 fd;
    __u32 len;
    __u64 user_data;
    __s32 verdict;
};

#endif
