#define _GNU_SOURCE

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../bpf/ringguard_policy.skel.h"
#include "../bpf/common.h"

static volatile bool exiting = false;
static bool quiet = false;
static unsigned long events_seen = 0;

static void sig_handler(int sig)
{
    exiting = true;
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
    const struct event *e = data;
    const char *op = "?";

    events_seen++;
    if (quiet)
        return 0;

    switch (e->opcode) {
    case 0: op = "NOP"; break;
    case 11: op = "TIMEOUT"; break;
    case 15: op = "LINK_TIMEOUT"; break;
    case 22: op = "READ"; break;
    case 23: op = "WRITE"; break;
    case 31: op = "PROVIDE_BUFFERS"; break;
    case 32: op = "REMOVE_BUFFERS"; break;
    default: break;
    }
    printf("[%s] pid=%u uid=%u opcode=%s(%u) fd=%d len=%u ud=%llu verdict=%d\n",
           e->verdict ? "DENY" : "ALLOW", e->pid, e->uid, op, e->opcode,
           e->fd, e->len, (unsigned long long)e->user_data, e->verdict);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    struct ringguard_policy_bpf *skel;
    struct ring_buffer *rb = NULL;
    struct ringguard_config cfg = {};
    int err;
    __u32 key = 0;

    cfg.log_enabled = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--block-opcode") && i + 1 < argc) {
            cfg.block_opcode = atoi(argv[++i]);
            cfg.block_enabled = 1;
        }
        else if (!strcmp(argv[i], "--max-len") && i + 1 < argc)
            cfg.max_len = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--deny-link-timeout"))
            cfg.deny_link_timeout = 1;
        else if (!strcmp(argv[i], "--quiet"))
            quiet = true;
        else if (!strcmp(argv[i], "--no-log"))
            cfg.log_enabled = 0;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = ringguard_policy_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = ringguard_policy_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        ringguard_policy_bpf__destroy(skel);
        return 1;
    }

    err = ringguard_policy_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs: %d\n", err);
        ringguard_policy_bpf__destroy(skel);
        return 1;
    }

    if (bpf_map__fd(skel->maps.cfg) >= 0) {
        bpf_map_update_elem(bpf_map__fd(skel->maps.cfg), &key, &cfg, BPF_ANY);
    }

    rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
    if (!rb) {
        err = -1;
        fprintf(stderr, "Failed to create ring buffer\n");
        goto cleanup;
    }

    printf("RingGuard policy loaded and attached\n");
    printf("block_opcode=%u max_len=%u deny_link_timeout=%u log_enabled=%u quiet=%d\n",
           cfg.block_opcode, cfg.max_len, cfg.deny_link_timeout,
           cfg.log_enabled, quiet);
    fflush(stdout);

    while (!exiting) {
        err = ring_buffer__poll(rb, 100);
        if (err == -EINTR)
            break;
    }

cleanup:
    ring_buffer__free(rb);
    ringguard_policy_bpf__destroy(skel);
    printf("RingGuard events=%lu\n", events_seen);
    fflush(stdout);
    return err < 0 ? -err : 0;
}
