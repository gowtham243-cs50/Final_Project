#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uring.h"

#define BATCH_MAX 64
#define BUF_SIZE 4096

static unsigned char buf[BUF_SIZE];

static int init_file(void)
{
    int fd = open("/tmp/rg_policy_test", O_CREAT | O_RDWR | O_TRUNC, 0600);

    if (fd < 0)
        return -1;
    memset(buf, 0x41, sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
        close(fd);
        return -1;
    }
    return fd;
}

static int prep_sqe(struct io_uring_sqe *sqe, const char *op, unsigned len,
                    int filefd, unsigned idx)
{
    sqe->user_data = idx;

    if (!strcmp(op, "nop")) {
        sqe->opcode = IORING_OP_NOP;
    } else if (!strcmp(op, "read")) {
        sqe->opcode = IORING_OP_READ;
        sqe->fd = filefd;
        sqe->off = 0;
        sqe->addr = (__u64)(uintptr_t)buf;
        sqe->len = len;
    } else if (!strcmp(op, "write")) {
        sqe->opcode = IORING_OP_WRITE;
        sqe->fd = filefd;
        sqe->off = 0;
        sqe->addr = (__u64)(uintptr_t)buf;
        sqe->len = len;
    } else if (!strcmp(op, "provide")) {
        sqe->opcode = IORING_OP_PROVIDE_BUFFERS;
        sqe->fd = 1;
        sqe->off = 0;
        sqe->addr = (__u64)(uintptr_t)buf;
        sqe->len = len;
        sqe->buf_group = 0;
    } else if (!strcmp(op, "link_timeout")) {
        sqe->opcode = IORING_OP_LINK_TIMEOUT;
        sqe->len = 1;
    } else {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *op;
    unsigned len, count, submitted = 0, ok = 0, eperm = 0, other = 0;
    struct rg_ring ring;
    int filefd = -1;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <nop|read|write|provide|link_timeout> <len> <count>\n",
                argv[0]);
        return 2;
    }

    op = argv[1];
    len = (unsigned)strtoul(argv[2], NULL, 0);
    count = (unsigned)strtoul(argv[3], NULL, 0);
    if (!count)
        return 2;

    if (!strcmp(op, "read") || !strcmp(op, "write")) {
        filefd = init_file();
        if (filefd < 0) {
            fprintf(stderr, "failed to prepare test file: %s\n", strerror(errno));
            return 2;
        }
    }

    if (rg_ring_init(&ring, BATCH_MAX) < 0) {
        fprintf(stderr, "io_uring_setup failed: %s\n", strerror(errno));
        if (filefd >= 0)
            close(filefd);
        return 2;
    }

    while (submitted < count) {
        unsigned batch = count - submitted;

        if (batch > BATCH_MAX)
            batch = BATCH_MAX;

        for (unsigned i = 0; i < batch; i++) {
            struct io_uring_sqe *sqe = rg_get_sqe(&ring);

            if (!sqe) {
                fprintf(stderr, "SQ full before batch complete\n");
                goto fail;
            }
            if (prep_sqe(sqe, op, len, filefd, submitted + i) < 0) {
                fprintf(stderr, "unknown op: %s\n", op);
                goto fail;
            }
        }

        if (rg_submit(&ring, batch) != (int)batch) {
            fprintf(stderr, "submit failed: %s\n", strerror(errno));
            goto fail;
        }
        if (rg_wait_cqes_status(&ring, batch, &ok, &eperm, &other) < 0) {
            fprintf(stderr, "wait failed: %s\n", strerror(errno));
            goto fail;
        }
        submitted += batch;
    }

    printf("POLICY_OP op=%s len=%u count=%u ok=%u eperm=%u other=%u\n",
           op, len, count, ok, eperm, other);
    rg_ring_destroy(&ring);
    if (filefd >= 0)
        close(filefd);
    return other ? 1 : 0;

fail:
    rg_ring_destroy(&ring);
    if (filefd >= 0)
        close(filefd);
    return 3;
}
