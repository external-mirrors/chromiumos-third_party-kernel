// SPDX-License-Identifier: GPL-2.0
/*
 * ISP IOCTL handling
 *
 * Copyright (C) Google LLC
 * Copyright (C) Intel Corporation
 */

#define pr_fmt(fmt) "isp-ioctl: " fmt

#include <linux/isp/isp-buffer.h>
#include <linux/isp/isp-device.h>
#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-ioctl.h>
#include <linux/isp/isp-namespace.h>
#include <linux/isp/isp-output.h>
#include <linux/isp/isp-syncfile.h>
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

#include <uapi/linux/isp.h>

static int isp_open(struct inode *inode, struct file *filp)
{
	struct isp_device *isp = container_of(inode->i_cdev,
					      struct isp_device, cdev);
	struct isp_fh *fh;
	int ret;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh)
		return -ENOMEM;

	ret = isp_pipeline_init(isp, &fh->pipeline);
	if (ret) {
		kfree(fh);
		return ret;
	}

	if (isp_pipeline_io_setup(&fh->pipeline)) {
		kfree(fh);
		return -EINVAL;
	}

	filp->private_data = fh;
	fh->isp = isp;

	return 0;
}

static int isp_release(struct inode *inode, struct file *filp)
{
	struct isp_fh *fh = filp->private_data;

	isp_pipeline_io_release(&fh->pipeline);
	isp_pipeline_destroy(&fh->pipeline);
	kfree(fh);

	return 0;
}

static bool is_valid_ioctlcmd_size(unsigned int cmd, struct isp_header *hdr,
				   size_t req_size)
{
	unsigned int length = _IOC_SIZE(cmd);
	size_t need_bytes;

	if (length < sizeof(struct isp_header))
		return false;

	need_bytes = sizeof(struct isp_header) + hdr->num_requests * req_size;
	return length == need_bytes;
}

static int isp_ioctl_query_result_copy(struct isp_query __user *payload,
				       struct isp_query *query)
{
	int ret;

	switch (query->query_type) {
	case ISP_QUERY_TYPE_ENTITIES:
		ret = put_user(query->query_entities.num_entities,
			       &payload->query_entities.num_entities);
		break;
	case ISP_QUERY_TYPE_EVENTS:
		ret = put_user(query->query_events.num_events,
			       &payload->query_events.num_events);
		break;
	case ISP_QUERY_TYPE_OPERATIONS:
		ret = put_user(query->query_operations.num_ops,
			       &payload->query_operations.num_ops);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int isp_ioctl_parse_query(struct isp_fh *fh, unsigned int cmd,
				 struct isp_header *hdr, void __user *uarg)
{
	struct isp_koutput output = {};
	struct isp_query __user *payload;
	u32 num_query;
	int ret = 0;

	if (!is_valid_ioctlcmd_size(cmd, hdr, sizeof(struct isp_query)))
		return -EINVAL;

	payload = uarg + sizeof(struct isp_header);
	if (isp_output_init(hdr, &output))
		return -EFAULT;

	for (num_query = 0; num_query < hdr->num_requests; num_query++) {
		struct isp_query query;

		if (copy_from_user(&query, payload, sizeof(query))) {
			hdr->error = num_query;
			return -EFAULT;
		}

		switch (query.query_type) {
		case ISP_QUERY_TYPE_ENTITIES:
			isp_ns_enumeration_begin(fh->isp);
			ret = isp_enum_entities(fh->isp,
						&query.query_entities,
						&output);
			isp_ns_enumeration_end(fh->isp);
			break;
		case ISP_QUERY_TYPE_EVENTS:
			isp_ns_enumeration_begin(fh->isp);
			ret = isp_enum_events(fh->isp,
					      &query.query_events,
					      &output);
			isp_ns_enumeration_end(fh->isp);
			break;
		case ISP_QUERY_TYPE_OPERATIONS:
			/*
			 * OPERATIONS query is performed on a local namespace
			 * (pipeline namespace), so no enumeration begin/end
			 * is needed.
			 */
			ret = isp_enum_operations(&fh->pipeline,
						  &query.query_operations,
						  &output);
			break;
		case ISP_QUERY_TYPE_DMABUF:
			/*
			 * DMABUF query is performed on a local namespace
			 * (pipeline namespace), so no enumeration begin/end
			 * is needed.
			 */
			ret = isp_enum_buffer(&fh->pipeline,
					      &query.query_dmabuf,
					      &output);
			break;
		default:
			ret = -EINVAL;
		}

		if (ret || isp_ioctl_query_result_copy(payload, &query)) {
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
ALLOW_ERROR_INJECTION(isp_ioctl_parse_query, ERRNO);

/*
 * This will cancel successfully prepared OPs. Note that these OPs should not
 * be submitted.
 *
 * We need to cancel prepared OPs in reverse order, because tail OPs may have
 * dependencies on head OPs and hold their ref-counters.
 */
static int isp_ioctl_operation_cancel(struct isp_fh *fh,
				      struct isp_header *hdr,
				      struct isp_operation __user *payload,
				      u32 error_num)
{
	u32 num_op;

	/* Move payload to point to the last OP */
	payload += hdr->num_requests - 1;
	num_op = hdr->num_requests - 1;

	while (1) {
		struct isp_operation op;

		if (copy_from_user(&op, payload, sizeof(op)))
			return -EFAULT;

		switch (op.operation_type) {
		case ISP_OPERATION_TYPE_ADD:
			isp_pipeline_enqueue_cancel(&fh->pipeline,
						    &op.operation_add);
			break;
		}

		if (num_op == error_num)
			break;

		payload--;
		num_op--;
	}

	return 0;
}

static int isp_ioctl_operation_prepare(struct isp_fh *fh,
				       struct isp_header *hdr,
				       struct isp_operation __user *payload)
{
	u32 num_op;
	int ret;

	if (hdr->num_requests == 0)
		return 0;

	/*
	 * Prepare stage iterates over operations in direct mode (the
	 * way they appear in the batch), because at this stage we setup
	 * execution context for the entire operations batch, and later
	 * operations may depend on entity instances or DMA buffers that
	 * earlier operations create/import.
	 */
	for (num_op = 0; num_op < hdr->num_requests; num_op++) {
		struct isp_operation op;

		if (copy_from_user(&op, payload, sizeof(op)))
			return -EFAULT;

		switch (op.operation_type) {
		case ISP_OPERATION_TYPE_ADD:
			ret = isp_pipeline_enqueue_prepare(&fh->pipeline,
							   &op.operation_add);
			break;
		case ISP_OPERATION_TYPE_REMOVE:
			ret = isp_pipeline_dequeue(&fh->pipeline,
						   &op.operation_remove);
			break;
		default:
			ret = -EINVAL;
		}

		if (ret) {
			hdr->error = num_op;
			return ret;
		}

		payload++;
	}

	return 0;
}

static int isp_ioctl_operation_submit(struct isp_fh *fh,
				      struct isp_header *hdr,
				      struct isp_operation __user *payload)
{
	u32 num_op;
	int ret;

	if (hdr->num_requests == 0)
		return 0;

	/*
	 * Submit stage, on the contrary, iterates over operations in
	 * reverse order. Operations that come later in the batch can
	 * depend on one-time events that are triggered by already
	 * submitted and probably executed operations. Since operation
	 * execution is asynchronous, by the time we submit later batch
	 * operations it may be already too late to activate dependencies
	 * on earlier operations.
	 */
	payload += hdr->num_requests - 1;
	for (num_op = hdr->num_requests; num_op > 0; num_op--) {
		struct isp_operation op;

		if (copy_from_user(&op, payload, sizeof(op)))
			return -EFAULT;

		switch (op.operation_type) {
		case ISP_OPERATION_TYPE_ADD:
			ret = isp_pipeline_enqueue_submit(&fh->pipeline,
							  &op.operation_add);
			break;
		case ISP_OPERATION_TYPE_REMOVE:
			ret = 0;
			break;
		default:
			ret = -EINVAL;
		}

		if (ret) {
			hdr->error = num_op - 1;
			return ret;
		}

		payload--;
	}

	return ret;
}

static int isp_ioctl_parse_operation(struct isp_fh *fh, unsigned int cmd,
				     struct isp_header *hdr, void __user *uarg)
{
	struct isp_operation __user *payload;
	int ret;

	if (!is_valid_ioctlcmd_size(cmd, hdr, sizeof(struct isp_operation)))
		return -EINVAL;

	payload = uarg + sizeof(struct isp_header);
	ret = isp_ioctl_operation_prepare(fh, hdr, payload);
	if (ret) {
		/*
		 * We failed at prepare() stage. All OPs in this IOCTL
		 * can be cancelled as none of them have been submitted
		 * yet.
		 */
		payload = uarg + sizeof(struct isp_header);
		isp_ioctl_operation_cancel(fh, hdr, payload, 0);
		return ret;
	}

	payload = uarg + sizeof(struct isp_header);
	return isp_ioctl_operation_submit(fh, hdr, payload);
}
ALLOW_ERROR_INJECTION(isp_ioctl_parse_operation, ERRNO);

static int isp_ioctl_parse(struct isp_fh *fh, unsigned int cmd,
			   struct isp_header *hdr, void __user *uarg)
{
	switch (cmd & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) {
	case ISP_IOC_QUERY(0):
		return isp_ioctl_parse_query(fh, cmd, hdr, uarg);
	case ISP_IOC_OPERATION(0):
		return isp_ioctl_parse_operation(fh, cmd, hdr, uarg);
	}

	return -ENOIOCTLCMD;
}

static long isp_ioctl(struct file *filp, unsigned int cmd, unsigned long __uarg)
{
	struct isp_fh *fh = filp->private_data;
	void __user *uarg = (void __user *)__uarg;
	unsigned int dir = _IOC_DIR(cmd);
	struct isp_header hdr;
	int ret;

	ret = isp_device_uapi_call_enter(fh->isp);
	if (ret < 0)
		return ret;

	if (copy_from_user(&hdr, uarg, sizeof(hdr))) {
		ret = -EFAULT;
		goto done;
	}

	/* All ISP IOCTLs are _IOC_WRITE. */
	if (!(dir & _IOC_WRITE)) {
		ret = -EINVAL;
		goto done;
	}

	ret = isp_ioctl_parse(fh, cmd, &hdr, uarg);

	if (copy_to_user(uarg, &hdr, sizeof(hdr))) {
		ret = -EFAULT;
		goto done;
	}

done:
	isp_device_uapi_call_exit(fh->isp);
	return ret;
}

static ssize_t isp_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *filp = iocb->ki_filp;
	struct isp_fh *fh = filp->private_data;
	struct isp_ringbuffer *events_rb;
	size_t written = 0;
	size_t size;

	events_rb = &fh->pipeline.event_buffer;
	while ((size = iov_iter_count(iter)) > 0) {
		struct isp_completion completion = {};
		size_t copied;
		int ret;

		if (size < sizeof(completion))
			break;

		ret = isp_ringbuffer_read(events_rb, &completion,
					  iocb->ki_flags);
		if (ret)
			return written ? written : ret;

		copied = copy_to_iter(&completion, sizeof(completion), iter);
		if (copied == sizeof(completion))
			written += sizeof(struct isp_completion);
		else
			return written ? written : -EFAULT;

		if (signal_pending(current))
			return written ? written : -EINTR;
	}

	return written;
}

static __poll_t isp_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct isp_fh *fh = filp->private_data;
	struct isp_ringbuffer *events_rb;

	events_rb = &fh->pipeline.event_buffer;
	poll_wait(filp, &events_rb->wait, wait);

	if (isp_ringbuffer_has_entry(events_rb))
		return EPOLLIN | EPOLLRDNORM;

	return 0;
}

const struct file_operations isp_file_operations = {
	.owner = THIS_MODULE,
	.open = isp_open,
	.unlocked_ioctl = isp_ioctl,
	.release = isp_release,
	.llseek = no_llseek,
	.poll = isp_poll,
	.read_iter = isp_read_iter,
};
