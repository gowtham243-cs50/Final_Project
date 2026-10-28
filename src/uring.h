#ifndef RINGGUARD_URING_H
#define RINGGUARD_URING_H

#include <linux/io_uring.h>
#include <stddef.h>
#include <stdint.h>

struct rg_ring {
    int fd;
    unsigned entries;
    struct io_uring_params p;
    void *sq_ring;
    void *cq_ring;
    struct io_uring_sqe *sqes;
    size_t sq_ring_sz;
    size_t cq_ring_sz;
    size_t sqes_sz;
    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_mask;
    unsigned *sq_array;
    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_mask;
    struct io_uring_cqe *cqes;
};

int rg_ring_init(struct rg_ring *r, unsigned entries);
void rg_ring_destroy(struct rg_ring *r);
struct io_uring_sqe *rg_get_sqe(struct rg_ring *r);
int rg_submit(struct rg_ring *r, unsigned n);
int rg_wait_cqes(struct rg_ring *r, unsigned n);
int rg_wait_cqes_status(struct rg_ring *r, unsigned n,
                        unsigned *ok, unsigned *eperm, unsigned *other);
unsigned rg_cq_ready(struct rg_ring *r);

#endif
