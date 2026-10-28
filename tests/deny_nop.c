#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uring.h"

int main(int argc, char **argv)
{
    unsigned count = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 100;
    struct rg_ring ring;
    unsigned submitted = 0, ok = 0, eperm = 0, other = 0;

    if (!count)
        count = 100;
    if (rg_ring_init(&ring, 64) < 0) {
        fprintf(stderr, "io_uring_setup failed: %s\n", strerror(errno));
        return 2;
    }

    while (submitted < count) {
        unsigned batch = count - submitted;

        if (batch > 64)
            batch = 64;

        for (unsigned i = 0; i < batch; i++) {
            struct io_uring_sqe *sqe = rg_get_sqe(&ring);

            if (!sqe) {
                fprintf(stderr, "SQ full before batch complete\n");
                rg_ring_destroy(&ring);
                return 3;
            }
            sqe->opcode = IORING_OP_NOP;
            sqe->user_data = submitted + i;
        }

        if (rg_submit(&ring, batch) != (int)batch) {
            fprintf(stderr, "submit failed: %s\n", strerror(errno));
            rg_ring_destroy(&ring);
            return 4;
        }
        if (rg_wait_cqes_status(&ring, batch, &ok, &eperm, &other) < 0) {
            fprintf(stderr, "wait failed: %s\n", strerror(errno));
            rg_ring_destroy(&ring);
            return 5;
        }
        submitted += batch;
    }

    printf("DENY_TEST count=%u ok=%u eperm=%u other=%u\n",
           count, ok, eperm, other);
    rg_ring_destroy(&ring);
    return other ? 1 : 0;
}
