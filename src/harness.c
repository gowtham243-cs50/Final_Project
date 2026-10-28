#define _GNU_SOURCE

#include "uring.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [count] [batch] [reps]\n"
            "  count  total NOP requests per run (default 10000)\n"
            "  batch  requests per io_uring_enter  (default 64)\n"
            "  reps   timing repetitions           (default 10)\n",
            prog);
}

int main(int argc, char **argv)
{
    unsigned count = 10000;
    unsigned batch = 64;
    unsigned reps = 10;
    unsigned entries = 4096;

    if (argc > 1)
        count = (unsigned)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        batch = (unsigned)strtoul(argv[2], NULL, 0);
    if (argc > 3)
        reps = (unsigned)strtoul(argv[3], NULL, 0);
    if (!count || !batch || !reps || batch > entries) {
        usage(argv[0]);
        return 2;
    }

    struct rg_ring ring;
    if (rg_ring_init(&ring, entries) < 0) {
        fprintf(stderr, "io_uring_setup failed: %s\n", strerror(errno));
        return 1;
    }

    printf("rep,count,batch,total_ns,ns_per_req,errors\n");
    fflush(stdout);

    for (unsigned rep = 0; rep < reps; rep++) {
        unsigned errors = 0;
        unsigned submitted = 0;
        long long start = now_ns();

        while (submitted < count) {
            unsigned n = count - submitted;
            if (n > batch)
                n = batch;

            for (unsigned i = 0; i < n; i++) {
                struct io_uring_sqe *sqe = rg_get_sqe(&ring);
                if (!sqe) {
                    fprintf(stderr, "SQ full before batch complete\n");
                    rg_ring_destroy(&ring);
                    return 1;
                }
                sqe->opcode = IORING_OP_NOP;
                sqe->user_data = (unsigned long long)(submitted + i);
            }

            int ret = rg_submit(&ring, n);
            if (ret < 0) {
                fprintf(stderr, "io_uring_enter submit failed: %s\n",
                        strerror(errno));
                rg_ring_destroy(&ring);
                return 1;
            }
            if ((unsigned)ret != n) {
                fprintf(stderr, "short submit: %d of %u\n", ret, n);
                rg_ring_destroy(&ring);
                return 1;
            }

            if (rg_wait_cqes(&ring, n) < 0) {
                fprintf(stderr, "io_uring_enter wait failed: %s\n",
                        strerror(errno));
                rg_ring_destroy(&ring);
                return 1;
            }
            submitted += n;
        }

        long long elapsed = now_ns() - start;
        double per = (double)elapsed / (double)count;
        printf("%u,%u,%u,%lld,%.2f,%u\n", rep, count, batch, elapsed, per,
               errors);
        fflush(stdout);
    }

    rg_ring_destroy(&ring);
    return 0;
}
