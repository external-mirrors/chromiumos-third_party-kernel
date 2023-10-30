/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP events ring buffer
 *
 * Copyright (C) Google LLC
 */

#ifndef __LINUX_ISP_RINGBUFFER_H__
#define __LINUX_ISP_RINGBUFFER_H__

#include <linux/spinlock.h>
#include <linux/wait.h>

#define ISP_RINGBUFFER_SIZE	PAGE_SIZE

/**
 * struct isp_ringbuffer - ring buffer implementation
 */
struct isp_ringbuffer {
	/** @buffer: the actual buffer */
	u8			*buffer;
	/** @head: buffer head */
	off_t			head;
	/** @tail: buffer tail */
	off_t			tail;
	/** @seqno: a sequential number of the entry */
	atomic64_t		seqno;
	/** @lock: spin lock to protect buffer operations */
	spinlock_t		lock;
	/** @wait: the wait queue object */
	wait_queue_head_t	wait;
};

bool isp_ringbuffer_has_entry(struct isp_ringbuffer *rb);

int isp_ringbuffer_read(struct isp_ringbuffer *rb,
			struct isp_completion *completion,
			u32 flags);
int isp_ringbuffer_write(struct isp_ringbuffer *rb,
			 struct isp_completion *completion);

void isp_ringbuffer_release(struct isp_ringbuffer *rb);
int isp_ringbuffer_init(struct isp_ringbuffer *rb);

#endif /* __LINUX_ISP_RINGBUFFER_H__ */
