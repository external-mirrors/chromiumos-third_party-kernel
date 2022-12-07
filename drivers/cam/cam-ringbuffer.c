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

bool cam_ringbuffer_has_entry(struct cam_ringbuffer *rb)
{
	off_t head, tail;

	head = smp_load_acquire(&rb->head);
	tail = rb->tail;

	return CIRC_CNT(head, tail, rb->buffer_sz) >= rb->entry_sz;
}

int cam_ringbuffer_read(struct cam_ringbuffer *rb,
			struct cam_completion *completion,
			u32 flags)
{
	off_t tail;

	if (flags & IOCB_NOWAIT) {
		if (!cam_ringbuffer_has_entry(rb))
			return -EAGAIN;
	} else {
		if (wait_event_interruptible(rb->wait,
					     cam_ringbuffer_has_entry(rb)))
			return -EINTR;
	}

	spin_lock(&rb->lock);
	tail = rb->tail;
	memcpy(completion, &rb->buffer[tail], rb->entry_sz);
	smp_store_release(&rb->tail,
			  (tail + rb->entry_sz) & (rb->buffer_sz - 1));
	spin_unlock(&rb->lock);
	return 0;
}
ALLOW_ERROR_INJECTION(cam_ringbuffer_read, ERRNO);

int cam_ringbuffer_write(struct cam_ringbuffer *rb,
			 struct cam_completion *completion)
{
	off_t head, tail;

	spin_lock(&rb->lock);
	head = rb->head;
	tail = READ_ONCE(rb->tail);

	completion->seqno = atomic64_read(&rb->seqno);
	atomic64_inc(&rb->seqno);

	if (CIRC_SPACE(head, tail, rb->buffer_sz) <= rb->entry_sz) {
		/*
		 * We just move ahead, user-space should consume completions
		 * and see a gap in seqno
		 */
		smp_store_release(&rb->tail,
				  (tail + rb->entry_sz) & (rb->buffer_sz - 1));
	}

	memcpy(&rb->buffer[head], completion, rb->entry_sz);
	smp_store_release(&rb->head,
			  (head + rb->entry_sz) & (rb->buffer_sz - 1));
	spin_unlock(&rb->lock);

	if (waitqueue_active(&rb->wait))
		wake_up(&rb->wait);
	return 0;
}
ALLOW_ERROR_INJECTION(cam_ringbuffer_write, ERRNO);

void cam_ringbuffer_release(struct cam_ringbuffer *rb)
{
	if (WARN_ON(waitqueue_active(&rb->wait)))
		wake_up(&rb->wait);
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
