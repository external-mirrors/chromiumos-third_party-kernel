// SPDX-License-Identifier: GPL-2.0
/*
 * CAM completion events ring buffer
 *
 * Copyright (C) 2022 Google LLC
 */

#define pr_fmt(fmt) "cam-ringbuffer: " fmt

#include <linux/cam/cam-device.h>
#include <linux/cam/cam-ringbuffer.h>
#include <linux/circ_buf.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>

#define CIRC_ADD(rb, pos)			\
	((pos) + (rb)->entry_sz) & ((rb)->buffer_sz - 1)

bool cam_ringbuffer_has_entry(struct cam_ringbuffer *rb)
{
	bool entries;

	spin_lock(&rb->lock);
	entries = (CIRC_CNT(rb->head, rb->tail, rb->buffer_sz) >= rb->entry_sz);
	spin_unlock(&rb->lock);

	return entries;
}

int cam_ringbuffer_read(struct cam_ringbuffer *rb,
			struct cam_completion *completion,
			u32 flags)
{
	spin_lock(&rb->lock);
	while (CIRC_CNT(rb->head, rb->tail, rb->buffer_sz) < rb->entry_sz) {
		if (flags & IOCB_NOWAIT) {
			spin_unlock(&rb->lock);
			return -EAGAIN;
		}

		spin_unlock(&rb->lock);
		if (wait_event_interruptible(rb->wait,
					     cam_ringbuffer_has_entry(rb)))
			return -EINTR;
		spin_lock(&rb->lock);
	}

	memcpy(completion, &rb->buffer[rb->tail], rb->entry_sz);
	rb->tail = CIRC_ADD(rb, rb->tail);
	spin_unlock(&rb->lock);

	return 0;
}
ALLOW_ERROR_INJECTION(cam_ringbuffer_read, ERRNO);

int cam_ringbuffer_write(struct cam_ringbuffer *rb,
			 struct cam_completion *completion)
{
	spin_lock(&rb->lock);
	completion->seqno = atomic64_read(&rb->seqno);
	atomic64_inc(&rb->seqno);

	if (CIRC_SPACE(rb->head, rb->tail, rb->buffer_sz) < rb->entry_sz) {
		/*
		 * We just move ahead, user-space should consume completions
		 * and see a gap in seqno
		 */
		rb->tail = CIRC_ADD(rb, rb->tail);
	}

	memcpy(&rb->buffer[rb->head], completion, rb->entry_sz);
	rb->head = CIRC_ADD(rb, rb->head);
	spin_unlock(&rb->lock);

	wake_up(&rb->wait);

	return 0;
}
ALLOW_ERROR_INJECTION(cam_ringbuffer_write, ERRNO);

void cam_ringbuffer_release(struct cam_ringbuffer *rb)
{
	smp_mb();
	/* This really should not happen */
	WARN_ON(waitqueue_active(&rb->wait));

	kvfree(rb->buffer);
}

int cam_ringbuffer_init(struct cam_ringbuffer *rb,
			size_t entry_size,
			size_t buffer_size)
{
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

	rb->entry_sz = entry_size;
	rb->buffer_sz = buffer_size;
	spin_lock_init(&rb->lock);

	return 0;
}

#ifdef CONFIG_CAM_KUNIT_TESTS
#include "cam-ringbuffer-test.c"
#endif
