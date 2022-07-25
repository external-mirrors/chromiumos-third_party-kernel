// SPDX-License-Identifier: GPL-2.0
/*
 * CAM IOCTL handling
 *
 * Copyright (C) 2022 Google LLC
 * Copyright (C) 2020 Intel Corporation
 */

#define pr_fmt(fmt) "cam-ioctl: " fmt

#include <linux/cam/cam-buffer.h>
#include <linux/cam/cam-device.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-ioctl.h>
#include <linux/cam/cam-namespace.h>
#include <linux/cam/cam-output.h>
#include <linux/cam/cam-syncfile.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uio.h>

#include <uapi/linux/cam.h>

static int cam_open(struct inode *inode, struct file *filp)
{
	struct cam_device *cam = container_of(inode->i_cdev,
					      struct cam_device, cdev);
	struct cam_fh *fh;

	/* We permit only one active CAM user at this point */
	if (atomic_inc_return(&cam->num_users) != 1) {
		atomic_dec(&cam->num_users);
		return -EINVAL;
	}

	if (cam_pipeline_io_setup(&cam->pipeline))
		return -EINVAL;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh) {
		cam_pipeline_io_release(&cam->pipeline);
		return -ENOMEM;
	}

	filp->private_data = fh;
	fh->cam = cam;

	return 0;
}

static int cam_release(struct inode *inode, struct file *filp)
{
	struct cam_fh *fh = filp->private_data;

	atomic_dec(&fh->cam->num_users);
	cam_pipeline_io_release(&fh->cam->pipeline);
	kfree(fh);

	return 0;
}

static bool is_valid_ioctlcmd_size(unsigned int cmd, struct cam_header *hdr,
				   size_t req_size)
{
	unsigned int length = _IOC_SIZE(cmd);
	size_t need_bytes;

	if (length < sizeof(struct cam_header))
		return false;

	need_bytes = sizeof(struct cam_header) + hdr->num_queries * req_size;
	return length == need_bytes;
}

static int cam_ioctl_parse_query(struct cam_fh *fh, unsigned int cmd,
				 struct cam_header *hdr, void __user *uarg)
{
	struct cam_koutput output = {0, };
	struct cam_query __user *payload;
	u32 num_query;
	int ret = 0;

	if (!is_valid_ioctlcmd_size(cmd, hdr, sizeof(struct cam_query)))
		return -EINVAL;

	payload = uarg + sizeof(struct cam_header);
	if (cam_output_init(hdr, &output))
		return -EFAULT;

	for (num_query = 0; num_query < hdr->num_queries; num_query++) {
		struct cam_query query;

		if (copy_from_user(&query, payload, sizeof(query))) {
			hdr->error = num_query;
			return -EFAULT;
		}

		switch (query.query_type) {
		case CAM_QUERY_TYPE_ENTITIES:
			ret = cam_enum_entities(fh->cam,
						&query.query_entities,
						&output);
			break;
		case CAM_QUERY_TYPE_EVENTS:
			ret = cam_enum_events(fh->cam,
					      &query.query_events,
					      &output);
			break;
		case CAM_QUERY_TYPE_OPERATIONS:
			ret = cam_pipeline_query(&fh->cam->pipeline,
						 &query.query_operations,
						 &output);
			break;
		default:
			ret = -EINVAL;
		}

		/* FIXME: do more reasonable copy-out */
		if (ret || copy_to_user(payload, &query, sizeof(query))) {
			hdr->error = num_query;
			return ret;
		}

		output.num_entries = 0;
		payload++;
	}

	hdr->output.length = output.length;
	if (output.length > hdr->output.size)
		return -ENOMEM;

	return ret;
}
ALLOW_ERROR_INJECTION(cam_ioctl_parse_query, ERRNO);

static int cam_ioctl_parse_operation(struct cam_fh *fh, unsigned int cmd,
				     struct cam_header *hdr, void __user *uarg)
{
	struct cam_operation __user *payload;
	u32 num_op;
	int ret = 0;

	if (!is_valid_ioctlcmd_size(cmd, hdr, sizeof(struct cam_operation)))
		return -EINVAL;

	payload = uarg + sizeof(struct cam_header);

	for (num_op = 0; num_op < hdr->num_queries; num_op++) {
		struct cam_operation op;

		if (copy_from_user(&op, payload, sizeof(op))) {
			hdr->error = num_op;
			return -EFAULT;
		}

		switch (op.operation_type) {
		case CAM_OPERATION_TYPE_ADD:
			ret = cam_pipeline_enqueue(&fh->cam->pipeline,
						   &op.operation_add);
			break;
		case CAM_OPERATION_TYPE_REMOVE:
			ret = cam_pipeline_dequeue(&fh->cam->pipeline,
						   &op.operation_remove);
			break;
		default:
			ret = -EINVAL;
		}

		/* FIXME: do more reasonable copy-out */
		if (ret || copy_to_user(payload, &op, sizeof(op))) {
			hdr->error = num_op;
			return ret;
		}

		payload++;
	}

	return ret;
}
ALLOW_ERROR_INJECTION(cam_ioctl_parse_operation, ERRNO);

static int cam_ioctl_parse(struct cam_fh *fh, unsigned int cmd,
			   struct cam_header *hdr, void __user *uarg)
{
	switch (cmd & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) {
	case CAM_IOC_QUERY(0):
		return cam_ioctl_parse_query(fh, cmd, hdr, uarg);
	case CAM_IOC_OPERATION(0):
		return cam_ioctl_parse_operation(fh, cmd, hdr, uarg);
	}

	return -ENOIOCTLCMD;
}

static long cam_ioctl(struct file *filp, unsigned int cmd, unsigned long __uarg)
{
	struct cam_fh *fh = filp->private_data;
	void __user *uarg = (void __user *)__uarg;
	unsigned int dir = _IOC_DIR(cmd);
	struct cam_header hdr;
	int ret;

	ret = cam_device_uapi_call_enter(fh->cam);
	if (ret < 0)
		return ret;

	if (copy_from_user(&hdr, uarg, sizeof(hdr))) {
		ret = -EFAULT;
		goto done;
	}

	/* All CAM IOCTLs are _IOC_WRITE. */
	if (!(dir & _IOC_WRITE)) {
		ret = -EINVAL;
		goto done;
	}

	ret = cam_ioctl_parse(fh, cmd, &hdr, uarg);

	if (copy_to_user(uarg, &hdr, sizeof(hdr))) {
		ret = -EFAULT;
		goto done;
	}

done:
	cam_device_uapi_call_exit(fh->cam);
	return ret;
}

static ssize_t cam_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *filp = iocb->ki_filp;
	struct cam_fh *fh = filp->private_data;
	struct cam_ringbuffer *events_rb;
	size_t written = 0;
	size_t size;

	events_rb = &fh->cam->pipeline.event_buffer;
	while ((size = iov_iter_count(iter)) > 0) {
		struct cam_completion completion = {};
		size_t copied;
		int ret;

		if (size < sizeof(completion))
			break;

		ret = cam_ringbuffer_read(events_rb, &completion,
					  iocb->ki_flags);
		if (ret)
			return written ? written : ret;

		copied = copy_to_iter(&completion, sizeof(completion), iter);
		if (copied == sizeof(completion))
			written += sizeof(struct cam_completion);
		else
			return written ? written : -EFAULT;

		if (signal_pending(current))
			return written ? written : -EINTR;
	}

	return written;
}

static __poll_t cam_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct cam_fh *fh = filp->private_data;
	struct cam_ringbuffer *events_rb;

	events_rb = &fh->cam->pipeline.event_buffer;
	poll_wait(filp, &events_rb->wait, wait);

	if (cam_ringbuffer_has_entry(events_rb))
		return EPOLLIN | EPOLLRDNORM;

	return 0;
}

const struct file_operations cam_file_operations = {
	.owner = THIS_MODULE,
	.open = cam_open,
	.unlocked_ioctl = cam_ioctl,
	.release = cam_release,
	.llseek = no_llseek,
	.poll = cam_poll,
	.read_iter = cam_read_iter,
};
