/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM events ring buffer
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __LINUX_CAM_RINGBUFFER_H__
#define __LINUX_CAM_RINGBUFFER_H__

#include <linux/spinlock.h>
#include <linux/wait.h>

#define CAM_RINGBUFFER_SIZE	PAGE_SIZE

/**
 * cam_ringbuffer - ring buffer implementation
 */
struct cam_ringbuffer {
	/** @buffer: the actual buffer */
	u8			*buffer;
	/**
	 * @buffer_sz: the buffer size, must be a power of two and
	 * at least twice as big as @entry_sz
	 */
	size_t			buffer_sz;
	/** @entry_sz: the entry size */
	size_t			entry_sz;
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

bool cam_ringbuffer_has_entry(struct cam_ringbuffer *rb);

int cam_ringbuffer_read(struct cam_ringbuffer *rb,
			struct cam_completion *completion,
			u32 flags);
int cam_ringbuffer_write(struct cam_ringbuffer *rb,
			 struct cam_completion *completion);

void cam_ringbuffer_release(struct cam_ringbuffer *rb);
int cam_ringbuffer_init(struct cam_ringbuffer *rb,
			size_t entry_size,
			size_t buffer_size);

#endif /* __LINUX_CAM_RINGBUFFER_H__ */
