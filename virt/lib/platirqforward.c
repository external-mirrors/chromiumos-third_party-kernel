// SPDX-License-Identifier: GPL-2.0
/*
 * Experimental driver for user space interrupt handler.
 *
 * Copyright 2020 Google LLC
 *
 */

#include <linux/acpi.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/vfio.h>
#include <linux/eventfd.h>
#include <linux/delay.h>
#include <uapi/linux/platirqforward.h>
#include <linux/plat_irqfd.h>

#define VERSION	"0.1"
#define AUTHOR	"Micah Morton <mortonm@chromium.org>"
#define DESC	"Platform IRQ Forwarding"

MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(DESC);
MODULE_ALIAS("devname:plat-irq-forward");

static DEFINE_MUTEX(plat_fwd_irq_mutex);
static LIST_HEAD(plat_fwd_irq_list);

static LIST_HEAD(gpes_list);
static struct plat_irq_forward *fixed_evts[ACPI_NUM_FIXED_EVENTS];

static int plat_irq_forward_unmask_handler_level(int irq, void *data)
{
	struct plat_irq_forward *l = data;
	unsigned long flags;

	spin_lock_irqsave(&(l->spinlock), flags);
	if (l->is_masked) {
		enable_irq(l->irq_num);
		l->is_masked = false;
	}
	spin_unlock_irqrestore(&(l->spinlock), flags);
	return 0;
}


static irqreturn_t plat_irq_forward_handler_level(int irq, void *data)
{
	struct plat_irq_forward *l = data;
	unsigned long flags;
	int ret = IRQ_NONE;

	spin_lock_irqsave(&(l->spinlock), flags);

	disable_irq_nosync(irq);
	l->is_masked = true;
	ret = IRQ_HANDLED;

	spin_unlock_irqrestore(&(l->spinlock), flags);

	if (ret == IRQ_HANDLED)
		eventfd_signal(l->trigger, 1);

	return ret;
}

static irqreturn_t plat_irq_forward_handler_edge(int irq, void *data)
{
	struct plat_irq_forward *e = data;

	eventfd_signal(e->trigger, 1);

	return IRQ_HANDLED;
}

static int plat_irq_forward_request(struct plat_irq_forward *irq)
{
	bool is_level = irq->flags & PLAT_IRQ_FORWARD_SET_LEVEL_TRIGGER_EVENTFD;
	irq_handler_t handler = is_level ? plat_irq_forward_handler_level :
					   plat_irq_forward_handler_edge;
	int trigger = is_level ? ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
	int flags = is_level ? IRQF_NO_AUTOEN : 0;

	int ret;

	ret = request_irq(irq->irq_num, handler, flags, irq->name, irq);
	if (ret == -EINVAL) {
		/* TODO: Retrieve polarity */
		ret = acpi_register_gsi(NULL, irq->irq_num, trigger,
					ACPI_ACTIVE_LOW);
		if (ret < 0)
			goto out;

		ret = request_irq(irq->irq_num, handler, flags, irq->name,
				  irq);
	}

out:
	if (ret)
		pr_err("%s: failed to configure IRQ=%d direct mode\n",
		       __func__, irq->irq_num);
	return ret;
}

static struct plat_irq_forward *plat_irq_forward_find(
						uint32_t irq_number_host)
{
	struct plat_irq_forward *irq;

	list_for_each_entry(irq, &plat_fwd_irq_list, list) {
		if (irq->irq_num == irq_number_host)
			return irq;
	}

	return NULL;
}

static void plat_irq_forward_del(uint32_t irq_number_host)
{
	struct plat_irq_forward *irq, *tmp;

	list_for_each_entry_safe(irq, tmp, &plat_fwd_irq_list, list) {
		if (irq->irq_num != irq_number_host)
			continue;

		if (irq->trigger) {
			irq_clear_status_flags(irq->irq_num,
					       IRQ_NOAUTOEN);
			free_irq(irq->irq_num, irq);
			kfree(irq->name);
			eventfd_ctx_put(irq->trigger);
		}
		list_del(&irq->list);
		kfree(irq);
		break;
	}
}

static int platform_set_irqs_ioctl_trigger(uint32_t irq_number_host, void *data,
					   uint32_t flags)
{
	bool is_level = flags & PLAT_IRQ_FORWARD_SET_LEVEL_TRIGGER_EVENTFD;
	int32_t fd = *(int32_t *)data;
	struct plat_irq_forward *irq;
	int error;

	/* Disable only */
	if (fd < 0) {
		mutex_lock(&plat_fwd_irq_mutex);
		plat_irq_forward_del(irq_number_host);
		mutex_unlock(&plat_fwd_irq_mutex);
		return 0;
	}

	mutex_lock(&plat_fwd_irq_mutex);
	irq = plat_irq_forward_find(irq_number_host);
	if (irq) {
		error = -EEXIST;
		goto err;
	}

	irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	if (!irq) {
		error = -ENOMEM;
		goto err;
	}

	irq->name = kasprintf(GFP_KERNEL, "%s-triggered-irq[%d]",
			      is_level ? "level" : "edge", irq_number_host);
	if (!irq->name) {
		error = -ENOMEM;
		goto err_free_irq;
	}

	irq->trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(irq->trigger)) {
		error = PTR_ERR(irq->trigger);
		goto err_free_name;
	}

	irq->irq_num = irq_number_host;
	irq->flags = flags;
	irq->is_masked = false;
	spin_lock_init(&irq->spinlock);
	list_add(&irq->list, &plat_fwd_irq_list);

	if (!(flags & PLAT_IRQ_FORWARD_SET_LEVEL_ACPI_SCI_TRIGGER_EVENTFD)) {
		error = plat_irq_forward_request(irq);
		if (error)
			goto err_put_ctx;
	}

	mutex_unlock(&plat_fwd_irq_mutex);
	return 0;

err_put_ctx:
	eventfd_ctx_put(irq->trigger);
	list_del(&irq->list);
err_free_name:
	kfree(irq->name);
err_free_irq:
	kfree(irq);
err:
	mutex_unlock(&plat_fwd_irq_mutex);
	return error;
}

static int platform_set_irqs_ioctl_level_unmask(uint32_t irq_number_host, void *data,
					 uint32_t flags)
{
	int32_t fd = *(int32_t *)data;
	struct plat_irq_forward *irq;
	int error;

	if (fd < 0)
		return -EINVAL;

	mutex_lock(&plat_fwd_irq_mutex);
	irq = plat_irq_forward_find(irq_number_host);
	if (!irq) {
		error = -ENXIO;
		goto err;
	}

	error = plat_irq_forward_irqfd_enable(
				plat_irq_forward_unmask_handler_level,
				irq, &irq->unmask, fd);

	if (!(flags & PLAT_IRQ_FORWARD_SET_LEVEL_ACPI_SCI_UNMASK_EVENTFD)) {
		/*
		 * TODO: Investigate whether we can postphone enabling
		 * until guest requests it.
		 */
		enable_irq(irq->irq_num);
		pr_info("%s: %d is enabled.\n", __func__, irq->irq_num);
	}

err:
	mutex_unlock(&plat_fwd_irq_mutex);
	return error;
}

int plat_irq_forward_set_irq_wake(uint32_t irq_number_host, bool on)
{
	struct plat_irq_forward *irq;

	/* Ignore requests for non-passthrough IRQs. */
	irq = plat_irq_forward_find(irq_number_host);
	if (!irq || (irq->flags & PLAT_IRQ_FORWARD_SET_LEVEL_ACPI_SCI_TRIGGER_EVENTFD))
		return 0;

	return irq_set_irq_wake(irq_number_host, on);
}
EXPORT_SYMBOL(plat_irq_forward_set_irq_wake);

static int plat_irq_forward_ioctl(void *device_data, unsigned long arg)
{
	u8 *data;
	unsigned long minsz;
	struct plat_irq_forward_set hdr;

	minsz = offsetofend(struct plat_irq_forward_set, count);

	if (copy_from_user(&hdr, (void __user *)arg, minsz))
		return -EFAULT;

	/* Just one FD is supported for now */
	if (hdr.count != 1)
		return -EINVAL;

	data = memdup_user((void __user *)(arg + minsz),
			   hdr.count * sizeof(int32_t));
	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (hdr.action_flags) {
	case PLAT_IRQ_FORWARD_SET_LEVEL_TRIGGER_EVENTFD:
		return platform_set_irqs_ioctl_trigger(
				hdr.irq_number_host, data,
				hdr.action_flags);
	case PLAT_IRQ_FORWARD_SET_LEVEL_UNMASK_EVENTFD:
		return platform_set_irqs_ioctl_level_unmask(
				hdr.irq_number_host, data,
				hdr.action_flags);
	case PLAT_IRQ_FORWARD_SET_EDGE_TRIGGER:
		return platform_set_irqs_ioctl_trigger(
				hdr.irq_number_host, data,
				hdr.action_flags);
	case PLAT_IRQ_FORWARD_SET_LEVEL_ACPI_SCI_TRIGGER_EVENTFD:
		return platform_set_irqs_ioctl_trigger(
				acpi_gbl_FADT.sci_interrupt, data,
				hdr.action_flags);
	case PLAT_IRQ_FORWARD_SET_LEVEL_ACPI_SCI_UNMASK_EVENTFD:
		return platform_set_irqs_ioctl_level_unmask(
				acpi_gbl_FADT.sci_interrupt, data,
				hdr.action_flags);
	default:
		return -EINVAL;
	}

	kfree(data);
	return 0;
}

static u32 acpi_event_handler_trigger_sci(struct plat_irq_forward *sci)
{
	unsigned long flags;
	bool already_masked;

	spin_lock_irqsave(&sci->spinlock, flags);

	/*
	 * Multiple GPEs and/or fixed events may be handled
	 * within a single SCI interrupt.
	 * Ensure that we trigger one SCI per one physical SCI
	 * and don't do nested masking of SCI irq.
	 */
	already_masked = sci->is_masked;
	if (!already_masked) {
		disable_irq_nosync(sci->irq_num);
		sci->is_masked = true;
	}

	spin_unlock_irqrestore(&sci->spinlock, flags);

	if (!already_masked)
		eventfd_signal(sci->trigger, 1);

	return ACPI_INTERRUPT_HANDLED;
}

static u32 gpe_forward_handler_level(acpi_handle dev, u32 gpe, void *data)
{
	struct gpe_fwd *gf = data;

	return acpi_event_handler_trigger_sci(gf->sci);
}

static u32 fixed_event_forward_handler_level(void *data)
{
	return acpi_event_handler_trigger_sci((struct plat_irq_forward *) data);
}

static struct gpe_fwd *gpe_fwd_find(uint32_t gpe)
{
	struct gpe_fwd *gf;

	list_for_each_entry(gf, &gpes_list, list) {
		if (gf->gpe == gpe)
			return gf;
	}

	return NULL;
}

static void gpe_fwd_del(uint32_t gpe)
{
	struct gpe_fwd *gf, *tmp;

	list_for_each_entry_safe(gf, tmp, &gpes_list, list) {
		if (gf->gpe != gpe)
			continue;

		acpi_remove_gpe_handler(NULL, gpe, &gpe_forward_handler_level);
		list_del(&gf->list);
		kfree(gf);
		break;
	}
}

static int gpe_set_ioctl_trigger(uint32_t gpe)
{
	struct plat_irq_forward *sci;
	struct gpe_fwd *gf;
	acpi_status status;
	int error;

	mutex_lock(&plat_fwd_irq_mutex);
	// We must already have a trigger for the SCI before we add a GPE
	sci = plat_irq_forward_find(acpi_gbl_FADT.sci_interrupt);
	if (!sci) {
		error = -ENXIO;
		goto err;
	}

	gf = gpe_fwd_find(gpe);
	if (gf) {
		error = -EEXIST;
		goto err;
	}

	gf = kzalloc(sizeof(*gf), GFP_KERNEL);
	if (!gf){
		error = -ENOMEM;
		goto err;
	}

	gf->gpe = gpe;
	gf->sci = sci;
	status = acpi_install_gpe_raw_handler(NULL, gpe,
					      ACPI_GPE_LEVEL_TRIGGERED,
					      &gpe_forward_handler_level, gf);
	if (ACPI_FAILURE(status)) {
		error = -EINVAL;
		goto free;
	}

	list_add(&gf->list, &gpes_list);

	mutex_unlock(&plat_fwd_irq_mutex);
	return 0;

free:
	kfree(gf);
err:
	mutex_unlock(&plat_fwd_irq_mutex);
	return error;
}

static void gpe_clear_ioctl_trigger(uint32_t gpe)
{
	mutex_lock(&plat_fwd_irq_mutex);
	gpe_fwd_del(gpe);
	mutex_unlock(&plat_fwd_irq_mutex);
}

static int fixed_event_set_ioctl_trigger(uint32_t event)
{
	struct plat_irq_forward *sci;
	acpi_status status;
	int ret;

	if (event > ACPI_EVENT_MAX)
		return -EINVAL;

	mutex_lock(&plat_fwd_irq_mutex);
	// We must already have a trigger for the SCI before we add a fixed event
	sci = plat_irq_forward_find(acpi_gbl_FADT.sci_interrupt);
	if (!sci) {
		ret = -ENXIO;
		goto out;
	}

	if (fixed_evts[event]) {
		ret = -EEXIST;
		goto out;
	}

	status = acpi_install_fixed_event_raw_handler(event,
						      fixed_event_forward_handler_level,
						      sci);
	if (ACPI_FAILURE(status)) {
		ret = -EINVAL;
		goto out;
	}

	fixed_evts[event] = sci;
	ret = 0;

out:
	mutex_unlock(&plat_fwd_irq_mutex);
	return ret;
}

static void fixed_event_clear_ioctl_trigger(uint32_t event)
{
	if (event > ACPI_EVENT_MAX)
		return;

	mutex_lock(&plat_fwd_irq_mutex);

	if (fixed_evts[event]) {
		acpi_remove_fixed_event_handler(event, fixed_event_forward_handler_level);
		fixed_evts[event] = NULL;
	}

	mutex_unlock(&plat_fwd_irq_mutex);
}

static int acpi_evt_forward_ioctl(void *device_data, unsigned long arg)
{
	struct acpi_evt_forward_set hdr;

	if (copy_from_user(&hdr, (void __user *)arg, sizeof(hdr)))
		return -EFAULT;

	switch (hdr.action_flags) {
	case ACPI_EVT_FORWARD_SET_GPE_TRIGGER:
		return gpe_set_ioctl_trigger(hdr.gpe_host_nr);
	case ACPI_EVT_FORWARD_CLEAR_GPE_TRIGGER:
		gpe_clear_ioctl_trigger(hdr.gpe_host_nr);
		break;
	case ACPI_EVT_FORWARD_SET_FIXED_EVENT_TRIGGER:
		return fixed_event_set_ioctl_trigger(hdr.fixed_evt_nr);
	case ACPI_EVT_FORWARD_CLEAR_FIXED_EVENT_TRIGGER:
		fixed_event_clear_ioctl_trigger(hdr.fixed_evt_nr);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * Platform IRQ Forwarding fd, /dev/plat-irq-forward
 */
static long plat_irq_forward_fops_unl_ioctl(struct file *filep,
		unsigned int cmd, unsigned long arg)
{
	long ret = -EINVAL;

	switch (cmd) {
	case PLAT_IRQ_FORWARD_SET:
		ret = plat_irq_forward_ioctl(filep, arg);
		break;
	case ACPI_EVT_FORWARD_SET:
		ret = acpi_evt_forward_ioctl(filep, arg);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long plat_irq_forward_fops_compat_ioctl(struct file *filep,
		unsigned int cmd, unsigned long arg)
{
	arg = (unsigned long)compat_ptr(arg);
	return plat_irq_forward_fops_unl_ioctl(filep, cmd, arg);
}
#endif	/* CONFIG_COMPAT */

static int plat_irq_forward_fops_open(struct inode *inode, struct file *filep)
{
	return 0;
}

static int plat_irq_forward_fops_release(struct inode *inode,
					 struct file *filep)
{
	return 0;
}

static const struct file_operations plat_irq_forward_fops = {
	.owner		= THIS_MODULE,
	.open		= plat_irq_forward_fops_open,
	.release	= plat_irq_forward_fops_release,
	.unlocked_ioctl	= plat_irq_forward_fops_unl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= plat_irq_forward_fops_compat_ioctl,
#endif
};

static struct miscdevice plat_irq_forward_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "plat-irq-forward",
	.fops = &plat_irq_forward_fops,
	.nodename = "plat-irq-forward",
	.mode = 0660,
};

static int __init plat_irq_forward_init(void)
{
	int ret;

	ret = misc_register(&plat_irq_forward_dev);
	if (ret) {
		pr_err("plat-irq-forward: misc device register failed\n");
		return ret;
	}

	pr_info(DESC " version: " VERSION "\n");

	return 0;
}

// TODO: cleanup/free/disconnect stuff
static void __exit plat_irq_forward_cleanup(void)
{
	misc_deregister(&plat_irq_forward_dev);
}

module_init(plat_irq_forward_init);
module_exit(plat_irq_forward_cleanup);
