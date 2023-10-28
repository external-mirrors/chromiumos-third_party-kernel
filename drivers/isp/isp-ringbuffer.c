// SPDX-License-Identifier: GPL-2.0
/*
 * ISP completion events ring buffer
 *
 * Copyright (C) Google LLC
 */

#define pr_fmt(fmt) "isp-ringbuffer: " fmt

#include <linux/isp/isp-device.h>
#include <linux/isp/isp-ringbuffer.h>
#include <linux/circ_buf.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

#define CIRC_ADD(rb, pos)			\
	(((pos) + sizeof(struct isp_completion)) & ((rb)->buffer_sz - 1))

bool isp_ringbuffer_has_entry(struct isp_ringbuffer *rb)
{
	size_t entry_sz = sizeof(struct isp_completion);
	bool entries;

	spin_lock(&rb->lock);
	entries = (CIRC_CNT(rb->head, rb->tail, rb->buffer_sz) >= entry_sz);
	spin_unlock(&rb->lock);

	return entries;
}

int isp_ringbuffer_read(struct isp_ringbuffer *rb,
			struct isp_completion *completion,
			u32 flags)
{
	size_t entry_sz = sizeof(struct isp_completion);

	spin_lock(&rb->lock);
	while (CIRC_CNT(rb->head, rb->tail, rb->buffer_sz) < entry_sz) {
		if (flags & IOCB_NOWAIT) {
			spin_unlock(&rb->lock);
			return -EAGAIN;
		}

		spin_unlock(&rb->lock);
		if (wait_event_interruptible(rb->wait,
					     isp_ringbuffer_has_entry(rb)))
			return -EINTR;
		spin_lock(&rb->lock);
	}

	memcpy(completion, &rb->buffer[rb->tail], entry_sz);
	rb->tail = CIRC_ADD(rb, rb->tail);
	spin_unlock(&rb->lock);

	return 0;
}
ALLOW_ERROR_INJECTION(isp_ringbuffer_read, ERRNO);

int isp_ringbuffer_write(struct isp_ringbuffer *rb,
			 struct isp_completion *completion)
{
	size_t entry_sz = sizeof(struct isp_completion);

	spin_lock(&rb->lock);
	completion->seqno = atomic64_read(&rb->seqno);
	atomic64_inc(&rb->seqno);

	if (CIRC_SPACE(rb->head, rb->tail, rb->buffer_sz) < entry_sz) {
		/*
		 * We just move ahead, user-space should consume completions
		 * and detect a gap in sequential numbers.
		 */
		rb->tail = CIRC_ADD(rb, rb->tail);
	}

	memcpy(&rb->buffer[rb->head], completion, entry_sz);
	rb->head = CIRC_ADD(rb, rb->head);
	spin_unlock(&rb->lock);

	wake_up(&rb->wait);

	return 0;
}
ALLOW_ERROR_INJECTION(isp_ringbuffer_write, ERRNO);

void isp_ringbuffer_release(struct isp_ringbuffer *rb)
{
	WARN_ON_ONCE(wq_has_sleeper(&rb->wait));
	kvfree(rb->buffer);
}

int isp_ringbuffer_init(struct isp_ringbuffer *rb, size_t buffer_size)
{
	size_t entry_size = sizeof(struct isp_completion);

	if (!is_power_of_2(buffer_size) || !is_power_of_2(entry_size))
		return -EINVAL;
	if (!entry_size || buffer_size / entry_size <= 1)
		return -EINVAL;

	rb->head = 0;
	rb->tail = 0;
	atomic64_set(&rb->seqno, 0);
	init_waitqueue_head(&rb->wait);

	rb->buffer = kvzalloc(buffer_size, GFP_KERNEL);
	if (!rb->buffer)
		return -ENOMEM;

	rb->buffer_sz = buffer_size;
	spin_lock_init(&rb->lock);

	return 0;
}

#ifdef CONFIG_ISP_KUNIT_TESTS
#include "isp-ringbuffer-test.c"
#endif
