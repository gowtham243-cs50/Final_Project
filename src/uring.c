#define _GNU_SOURCE

#include "uring.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

static int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    return (int)syscall(__NR_io_uring_setup, entries, p);
}

static int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                          unsigned flags)
{
    return (int)syscall(__NR_io_uring_enter, fd, to_submit, min_complete,
                        flags, NULL, 0);
}

static size_t page_align(size_t size)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    return (size + page - 1) & ~(page - 1);
}

int rg_ring_init(struct rg_ring *r, unsigned entries)
{
    memset(r, 0, sizeof(*r));
    r->fd = -1;
    r->entries = entries;

    r->fd = io_uring_setup(entries, &r->p);
    if (r->fd < 0)
        return -1;

    size_t sq_sz = r->p.sq_off.array +
                   r->p.sq_entries * sizeof(unsigned);
    size_t cq_sz = r->p.cq_off.cqes +
                   r->p.cq_entries * sizeof(struct io_uring_cqe);
    r->sq_ring_sz = page_align(sq_sz);
    r->cq_ring_sz = page_align(cq_sz);

    if (r->p.features & IORING_FEAT_SINGLE_MMAP) {
        if (r->cq_ring_sz > r->sq_ring_sz)
            r->sq_ring_sz = r->cq_ring_sz;
        r->cq_ring_sz = r->sq_ring_sz;
    }

    r->sq_ring = mmap(NULL, r->sq_ring_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQ_RING);
    if (r->sq_ring == MAP_FAILED)
        goto fail;
    if (r->p.features & IORING_FEAT_SINGLE_MMAP)
        r->cq_ring = r->sq_ring;
    else {
        r->cq_ring = mmap(NULL, r->cq_ring_sz, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_POPULATE, r->fd,
                          IORING_OFF_CQ_RING);
        if (r->cq_ring == MAP_FAILED)
            goto fail;
    }

    r->sqes_sz = r->p.sq_entries * sizeof(struct io_uring_sqe);
    r->sqes = mmap(NULL, r->sqes_sz, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQES);
    if (r->sqes == MAP_FAILED)
        goto fail;

    r->sq_head = (unsigned *)((char *)r->sq_ring + r->p.sq_off.head);
    r->sq_tail = (unsigned *)((char *)r->sq_ring + r->p.sq_off.tail);
    r->sq_mask = (unsigned *)((char *)r->sq_ring + r->p.sq_off.ring_mask);
    r->sq_array = (unsigned *)((char *)r->sq_ring + r->p.sq_off.array);
    r->cq_head = (unsigned *)((char *)r->cq_ring + r->p.cq_off.head);
    r->cq_tail = (unsigned *)((char *)r->cq_ring + r->p.cq_off.tail);
    r->cq_mask = (unsigned *)((char *)r->cq_ring + r->p.cq_off.ring_mask);
    r->cqes = (struct io_uring_cqe *)((char *)r->cq_ring + r->p.cq_off.cqes);

    return 0;

fail:
    rg_ring_destroy(r);
    return -1;
}

void rg_ring_destroy(struct rg_ring *r)
{
    if (r->sqes && r->sqes != MAP_FAILED)
        munmap(r->sqes, r->sqes_sz);
    if (r->cq_ring && r->cq_ring != MAP_FAILED &&
        r->cq_ring != r->sq_ring)
        munmap(r->cq_ring, r->cq_ring_sz);
    if (r->sq_ring && r->sq_ring != MAP_FAILED)
        munmap(r->sq_ring, r->sq_ring_sz);
    if (r->fd >= 0)
        close(r->fd);
    memset(r, 0, sizeof(*r));
    r->fd = -1;
}

struct io_uring_sqe *rg_get_sqe(struct rg_ring *r)
{
    unsigned head = __atomic_load_n(r->sq_head, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(r->sq_tail, __ATOMIC_RELAXED);

    if (tail - head >= r->p.sq_entries)
        return NULL;

    unsigned idx = tail & *r->sq_mask;
    struct io_uring_sqe *sqe = &r->sqes[idx];
    memset(sqe, 0, sizeof(*sqe));
    r->sq_array[idx] = idx;
    __atomic_store_n(r->sq_tail, tail + 1, __ATOMIC_RELEASE);
    return sqe;
}

int rg_submit(struct rg_ring *r, unsigned n)
{
    int ret = io_uring_enter(r->fd, n, 0, 0);
    if (ret < 0)
        return -1;
    return ret;
}

unsigned rg_cq_ready(struct rg_ring *r)
{
    unsigned head = __atomic_load_n(r->cq_head, __ATOMIC_RELAXED);
    unsigned tail = __atomic_load_n(r->cq_tail, __ATOMIC_ACQUIRE);
    return tail - head;
}

int rg_wait_cqes_status(struct rg_ring *r, unsigned n,
                        unsigned *ok, unsigned *eperm, unsigned *other)
{
    unsigned done = 0;

    while (done < n) {
        int ret = io_uring_enter(r->fd, 0, n - done,
                                 IORING_ENTER_GETEVENTS);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        unsigned start = __atomic_load_n(r->cq_head, __ATOMIC_RELAXED);
        unsigned head = start;
        unsigned tail = __atomic_load_n(r->cq_tail, __ATOMIC_ACQUIRE);

        while (head != tail) {
            struct io_uring_cqe *cqe = &r->cqes[head & *r->cq_mask];

            if (cqe->res == -EPERM) {
                if (eperm)
                    (*eperm)++;
            } else if (cqe->res < 0) {
                if (other)
                    (*other)++;
            } else if (ok) {
                (*ok)++;
            }
            head++;
        }

        done += head - start;
        __atomic_store_n(r->cq_head, head, __ATOMIC_RELEASE);
    }
    return 0;
}

int rg_wait_cqes(struct rg_ring *r, unsigned n)
{
    return rg_wait_cqes_status(r, n, NULL, NULL, NULL);
}
