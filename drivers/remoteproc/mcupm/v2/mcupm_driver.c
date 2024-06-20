// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include "mcupm_driver.h"
#include "mcupm_plt.h"
#include "mcupm_ipi_id.h"
#include "mcupm_ipi_table.h"
#include "mcupm_timesync.h"

/* TODO: implement mcupm driver pdata*/
static struct platform_device *mcupm_pdev;
static spinlock_t mcupm_mbox_lock[MCUPM_MBOX_TOTAL];

int mcupm_plt_ackdata;

static int mtk_ipi_init(struct platform_device *pdev)
{
	int ret;
	u32 val, i;
	struct device *dev = &pdev->dev;
	char name[32];
	bool legacy_mbox_dt;
	unsigned int mbox_total_number;

	struct mtk_mbox_device *mbox_dev = &mcupm_mboxdev;

	if (!of_property_read_u32(dev->of_node, "mediatek,mbox-extend", &val)) {
		mbox_total_number = val;
		legacy_mbox_dt = false;
	} else {
		mbox_total_number = 8;
		legacy_mbox_dt = true;
	}

	if (mbox_total_number > MCUPM_MBOX_TOTAL) {
		dev_err(dev, "[MCUPM] mbox number out-range %d\n", mbox_total_number);
		return -EINVAL;
	}

	mbox_dev->count = mbox_total_number;
	mbox_dev->recv_count = mbox_total_number;
	mbox_dev->send_count = mbox_total_number;
	ret = mtk_mbox_probe(pdev, mbox_dev, 0);
	if (ret) {
		dev_err(dev, "[MCUPM] mbox(0) probe fail on mbox-0, ret %d\n", ret);
		return -EINVAL;
	}

	for (i = 1; i < mbox_total_number; i++) {
		struct resource *res;
		resource_size_t offset, res_size;
		void __iomem *base = NULL;
		struct mtk_mbox_info *minfo_table = mbox_dev->info_table;

		if (legacy_mbox_dt == false) {
			res = platform_get_resource_byname(pdev,
					IORESOURCE_MEM, "mbox0_base");
			if (!res) {
				dev_err(dev, "[MCUPM] could not get resource for mbox0_base\n");
				return -ENODEV;
			}

			res_size = resource_size(res);
			offset = res->start + (i * res_size);
			base = devm_ioremap(dev, offset, res_size);
		} else { /* legacy mbox */
			snprintf(name, sizeof(name), "mbox%d_base", i);
			res = platform_get_resource_byname(pdev,
					IORESOURCE_MEM, name);
			if (!res) {
				dev_err(dev, "[MCUPM] could not get resource for %s\n", name);
				return -ENODEV;
			}

			base = devm_ioremap_resource(dev, res);
		}
		if (IS_ERR(base)) {
			ret = PTR_ERR(base);
			dev_err(dev, "mbox-%d can't remap(%pa) size=%pa ret=%d\n", i,
				&offset,
				&res_size,
				ret);
			return ret;
		}
		ret = mtk_smem_init(pdev, mbox_dev, i,
							base,
							minfo_table->set_irq_reg,
							minfo_table->clr_irq_reg,
							minfo_table->send_status_reg,
							minfo_table->recv_status_reg);
		if (ret) {
			dev_err(dev, "[MCUPM] mbox smem init fail on mbox-%d, ret %d\n",
				i, ret);
			return -EINVAL;
		}
	}

	dev_dbg(dev, "[MCUPM] ipi register\n");
	ret = mtk_ipi_device_register(&mcupm_ipidev, pdev, mbox_dev,
				      mbox_total_number);
	if (ret) {
		dev_err(dev, "[MCUPM] ipi_dev_register fail, ret %d\n", ret);
		return -EINVAL;
	}

	/* Initialize mcupm ipi driver. Move to struct mtk_mcupm */
	ret = mtk_ipi_register(&mcupm_ipidev, CH_S_PLATFORM, NULL, NULL,
			       (void *) &mcupm_plt_ackdata);
	if (ret) {
		dev_err(dev, "[MCUPM] ipi_register fail on ipi %d, ret %d\n", CH_S_PLATFORM, ret);
		return -EINVAL;
	}

	/* Initialize spin_lock for MCUPM HELPER for internal use. */
	for (i = 0; i < mbox_total_number; i++)
		spin_lock_init(&mcupm_mbox_lock[i]);

	return 0;
}

/* MCUPM HELPER. User is apmcu_sspm_mailbox_read/write*/
int mcupm_mbox_read(unsigned int mbox, unsigned int slot, void *buf,
			unsigned int len)
{
	if (WARN_ON(len > (mcupm_mboxdev.pin_send_table[mbox]).msg_size)) {
		dev_err(&mcupm_pdev->dev, "mbox:%u warning\n", mbox);
		return -EINVAL;
	}

	return mtk_mbox_read(&mcupm_mboxdev, mbox, slot,
				buf, len * MBOX_SLOT_SIZE);
}

int mcupm_mbox_write(unsigned int mbox, unsigned int slot, void *buf,
			unsigned int len)
{
	unsigned long flags;
	unsigned int status;
	int ret;

	if (WARN_ON(len > (mcupm_mboxdev.pin_send_table[mbox]).msg_size) ||
		WARN_ON(!buf)) {
		dev_err(&mcupm_pdev->dev, "mbox:%u warning\n", mbox);
		return -EINVAL;
	}

	spin_lock_irqsave(&mcupm_mbox_lock[mbox], flags);
	status = mtk_mbox_check_send_irq(&mcupm_mboxdev, mbox,
				(mcupm_mboxdev.pin_send_table[mbox]).pin_index);
	if (status != 0) {
		spin_unlock_irqrestore(&mcupm_mbox_lock[mbox], flags);
		return MBOX_PIN_BUSY;
	}
	spin_unlock_irqrestore(&mcupm_mbox_lock[mbox], flags);

	ret = mtk_mbox_write(&mcupm_mboxdev, mbox, slot
				, buf, len * MBOX_SLOT_SIZE);
	if (ret != MBOX_DONE)
		return ret;
	/* Ensure that all writes to SRAM are committed */
	mb();

	return 0;
}

void *get_mcupm_ipidev(void)
{
	return &mcupm_ipidev;
}

static int mcupm_device_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	mcupm_pdev = pdev;

	ret = mtk_ipi_init(pdev);
	if (ret) {
		dev_err(dev, "[MCUPM] ipi interface init fail, ret %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "MCUPM is ready to service IPI\n");

	ret = mcupm_plt_module_init(dev);
	if (ret) {
		dev_err(dev, "[MCUPM] plt module init fail, ret %d\n", ret);
		return ret;
	}

	ret = mcupm_timesync_init(dev);
	if (ret) {
		dev_err(dev, "MCUPM timesync init fail\n");
		return ret;
	}
	return 0;
}
static int mcupm_device_remove(struct platform_device *pdev)
{
	/* TODO: implement remove ipi interface and memory */
	mcupm_plt_module_exit(&pdev->dev);
	return 0;
}

#if IS_ENABLED(CONFIG_PM)
static int mt6779_mcupm_suspend(struct device *dev)
{
	mcupm_timesync_suspend();
	return 0;
}

static int mt6779_mcupm_resume(struct device *dev)
{
	mcupm_timesync_resume();
	return 0;
}

static const struct dev_pm_ops mt6779_mcupm_dev_pm_ops = {
	.suspend = mt6779_mcupm_suspend,
	.resume  = mt6779_mcupm_resume,
};
#endif

static const struct of_device_id mcupm_of_match[] = {
	{ .compatible = "mediatek,mt8196-mcupm", },
	{},
};
/* Add this marco to insmod mcupm.ko */
MODULE_DEVICE_TABLE(of, mcupm_of_match);

static const struct platform_device_id mcupm_id_table[] = {
	{ "mcupm", 0},
	{ },
};

static struct platform_driver mtk_mcupm_driver = {
	.shutdown = NULL,
	.suspend = NULL,
	.resume = NULL,
	.probe = mcupm_device_probe,
	.remove = mcupm_device_remove,
	.driver = {
		.name = "mcupm",
		.owner = THIS_MODULE,
		.of_match_table = mcupm_of_match,
#if IS_ENABLED(CONFIG_PM)
		.pm = &mt6779_mcupm_dev_pm_ops,
#endif

	},
	.id_table = mcupm_id_table,
};

MODULE_DESCRIPTION("MEDIATEK Module MCUPM driver");
MODULE_LICENSE("GPL");

module_platform_driver(mtk_mcupm_driver);
