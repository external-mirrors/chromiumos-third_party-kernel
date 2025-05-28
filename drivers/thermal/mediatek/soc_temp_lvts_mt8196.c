// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 MediaTek Inc.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/thermal.h>
#ifdef GPU_POWERCHK
#include "gpueb_common.h"
#endif
#include "soc_temp_lvts_mt8196.h"

#define WDT_REQ_MODE			0x30
#define WDT_STATUS_MCU_THERMAL_RST	(1<<23)
#define WDT_REQ_MODE_KEY		0x33000000

/*==================================================
 * LVTS local common code
 *==================================================
 */
static int lvts_raw_to_temp(struct formula_coeff *co, unsigned int sensor_id,
	unsigned int msr_raw)
{
	/* This function returns degree mC */

	int temp;

	temp = (co->a[0] * ((unsigned long long)msr_raw)) >> 14;
	temp = temp + co->golden_temp * 500 - co->a[0];

	return temp;
}

static unsigned int lvts_temp_to_raw(struct formula_coeff *co, unsigned int sensor_id,
	int temp)
{
	unsigned int msr_raw = 0;

	msr_raw = (unsigned int)div_s64(((long long)co->a[sensor_id] << 14), (temp - (co->golden_temp * 500) +
		co->a[sensor_id]));

	return msr_raw;
}

static noinline int __used lvts_read_tc_msr_raw(unsigned int *msr_reg)
{
	if (msr_reg == 0)
		return 0;

	return readl(msr_reg) & MRS_RAW_MASK;
}

static int __used lvts_read_all_tc_temperature(struct lvts_data *lvts_data, bool in_isr)
{
	struct tc_settings *tc = lvts_data->tc;
	unsigned int i, j, s_index, msr_raw;
	int max_temp = THERMAL_TEMP_INVALID, current_temp;
	void __iomem *base;
	struct platform_ops *ops = &lvts_data->ops;

	for (i = 0; i < lvts_data->num_tc; i++) {
		base = GET_BASE_ADDR(i);
		for (j = 0; j < tc[i].num_sensor; j++) {
			if (tc[i].sensor_on_off[j] != SEN_ON)
				continue;

			s_index = tc[i].sensor_map[j];

			if (lvts_data->is_tsfdc_n3e_ver)
				msr_raw = lvts_read_tc_msr_raw(LVTSATP0_0 + base + 0x4 * j);
			else
				msr_raw = lvts_read_tc_msr_raw(LVTSMSR0_0 + base + 0x4 * j);

			if (msr_raw == 0)
				current_temp = THERMAL_TEMP_INVALID;
			else
				current_temp = ops->lvts_raw_to_temp(&(tc[i].coeff), j, msr_raw);

			if (current_temp > max_temp)
				max_temp = current_temp;
		}
	}

	return max_temp;
}

static int lvts_read_tc_temperature(struct lvts_data *lvts_data, unsigned int tz_id, bool in_isr)
{
	struct tc_settings *tc = lvts_data->tc;
	unsigned int i, j, msr_raw;
	unsigned int s_index = tz_id - 1;
	int current_temp;
	void __iomem *base;
	struct platform_ops *ops = &lvts_data->ops;

	for (i = 0; i < lvts_data->num_tc; i++) {
		base = GET_BASE_ADDR(i);
		for (j = 0; j < tc[i].num_sensor; j++) {
			if (s_index != tc[i].sensor_map[j])
				continue;
#ifdef GPU_POWERCHK
			if (lvts_data->gpu_power_ctrl_id)
				if (i == lvts_data->gpu_power_ctrl_id)
					if (mfg0_pwr_sta() != MFG0_PWR_ON)
						return THERMAL_TEMP_INVALID;
#endif /* GPU_POWERCHK */

			if (lvts_data->is_tsfdc_n3e_ver)
				msr_raw = lvts_read_tc_msr_raw(LVTSATP0_0 + base + 0x4 * j);
			else
				msr_raw = lvts_read_tc_msr_raw(LVTSMSR0_0 + base + 0x4 * j);

			if (msr_raw == 0)
				current_temp = THERMAL_TEMP_INVALID;
			else
				current_temp = ops->lvts_raw_to_temp(&(tc[i].coeff), j, msr_raw);

			return current_temp;
		}
	}

	return THERMAL_TEMP_INVALID;
}

static int soc_temp_lvts_read_temp(struct thermal_zone_device *tz, int *temperature)
{
	struct soc_temp_tz *lvts_tz = (struct soc_temp_tz *)tz->devdata;
	struct lvts_data *lvts_data = lvts_tz->lvts_data;

	if (lvts_tz->id == 0)
		*temperature = lvts_read_all_tc_temperature(lvts_data, false);
	else if (lvts_tz->id - 1 < lvts_data->num_sensor)
		*temperature = lvts_read_tc_temperature(lvts_data, lvts_tz->id, false);
	else
		return -EINVAL;

	return 0;
}

static void disable_all_sensing_points(struct lvts_data *lvts_data)
{
	unsigned int i;
	void __iomem *base;

	for (i = 0; i < lvts_data->num_tc; i++) {
		base = GET_BASE_ADDR(i);
		writel(DISABLE_SENSING_POINT, LVTSMONCTL0_0 + base);
	}
}

static void enable_all_sensing_points(struct lvts_data *lvts_data)
{
	struct device *dev = lvts_data->dev;
	struct tc_settings *tc = lvts_data->tc;
	unsigned int i, j, num;
	void __iomem *base;
	unsigned int flag;

	for (i = 0; i < lvts_data->num_tc; i++) {
		base = GET_BASE_ADDR(i);
		num = tc[i].num_sensor;

		if (num > ALL_SENSING_POINTS) {
			dev_err(dev,
				"%s, LVTS%d, illegal number of sensors: %d\n",
				__func__, i, tc[i].num_sensor);
			continue;
		}

		flag = LVTS_SINGLE_SENSE;
		for (j = 0; j < tc[i].num_sensor; j++) {
			if (tc[i].sensor_on_off[j] != SEN_ON)
				continue;

			flag = flag | (0x1<<j);
		}
		writel(flag, LVTSMONCTL0_0 + base);
	}
}

/*
 * lvts_thermal_check_all_sensing_point_idle -
 * Check if all sensing points are idle
 * Return: 0 if all sensing points are idle
 *         an error code if one of them is busy
 * error code[31:16]: an index of LVTS thermal controller
 * error code[2]: bit 10 of LVTSMSRCTL1
 * error code[1]: bit 7 of LVTSMSRCTL1
 * error code[0]: bit 0 of LVTSMSRCTL1
 */
static int lvts_thermal_check_all_sensing_point_idle(struct lvts_data *lvts_data)
{
	int i, temp, error_code;
	void __iomem *base;

	for (i = 0; i < lvts_data->num_tc; i++) {
		base = GET_BASE_ADDR(i);
		temp = readl(base + LVTSMSRCTL1_0);
		/* Check if bit10=bit7=bit0=0 */
		if ((temp & 0x481) != 0) {
			error_code = (i << 16) + ((temp & BIT(10)) >> 8) +
				((temp & BIT(7)) >> 6) +
				(temp & BIT(0));

			return error_code;
		}
	}

	return 0;
}

void lvts_wait_for_all_sensing_point_idle(struct lvts_data *lvts_data)
{
	struct device *dev = lvts_data->dev;
	int cnt = 0, i, error_code, mask;
	int temp;
	void __iomem *base;

	mask = BIT(10) | BIT(7) | BIT(0);
	/*
	 * Wait until all sensoring points idled.
	 * No need to check LVTS status when suspend/resume,
	 * this will spend extra 100us of suspend flow.
	 * LVTS status will be reset after resume.
	 */
	while (cnt < 80) {
		temp = lvts_thermal_check_all_sensing_point_idle(lvts_data);
		if (temp == 0)
			goto TAIL;

		udelay(2);
		cnt++;
	}
	for (i = 0; i < lvts_data->num_tc; i++) {
		base = GET_BASE_ADDR(i);
		temp = readl(LVTSMSRCTL1_0 + base);
		if ((temp & mask) != 0) {
			error_code = ((temp & BIT(10)) >> 8) +
				((temp & BIT(7)) >> 6) +
				(temp & BIT(0));

			dev_info(dev, "Error LVTS %d sensing points aren't idle, error_code %d\n",
				i, error_code);
		}
	}
TAIL:
	return;
}

static int get_dominator_index(struct lvts_data *lvts_data, int tc_id)
{
	struct device *dev = lvts_data->dev;
	struct tc_settings *tc = lvts_data->tc;
	int d_index;

	if (tc[tc_id].dominator_sensing_point == ALL_SENSING_POINTS) {
		d_index = ALL_SENSING_POINTS;
	} else if (tc[tc_id].dominator_sensing_point <
		tc[tc_id].num_sensor){
		d_index = tc[tc_id].dominator_sensing_point;
	} else {
		dev_err(dev,
			"Error: LVTS%d, dominator_sensing_point= %d should smaller than num_sensor= %d\n",
			tc_id, tc[tc_id].dominator_sensing_point,
			tc[tc_id].num_sensor);

		dev_err(dev, "Use the sensing point 0 as the dominated sensor\n");
		d_index = SENSING_POINT0;
	}

	return d_index;
}

static void disable_hw_reboot_interrupt(struct lvts_data *lvts_data, int tc_id)
{
	unsigned int temp;
	void __iomem *base;

	base = GET_BASE_ADDR(tc_id);

	/* LVTS thermal controller has two interrupts for thermal HW reboot
	 * One is for AP SW and the other is for RGU
	 * The interrupt of AP SW can turn off by a bit of a register, but
	 * the other for RGU cannot.
	 * To prevent rebooting device accidentally, we are going to add
	 * a huge offset to LVTS and make LVTS always report extremely low
	 * temperature.
	 */

	/* After adding the huge offset 0x3FFF, LVTS alawys adds the
	 * offset to MSR_RAW.
	 * When MSR_RAW is larger, SW will convert lower temperature/
	 */
	temp = readl(LVTSPROTCTL_0 + base);
	writel(temp | 0x3FFF, LVTSPROTCTL_0 + base);

	/* Disable the interrupt of AP SW */
	temp = readl(LVTSMONINT_0 + base);

	temp = temp & ~(STAGE3_INT_EN);

	if (lvts_data->enable_dump_log) {
		temp = temp & ~(HIGH_OFFSET3_INT_EN |
						HIGH_OFFSET2_INT_EN |
						HIGH_OFFSET1_INT_EN |
						HIGH_OFFSET0_INT_EN);

		temp = temp & ~(LOW_OFFSET3_INT_EN |
						LOW_OFFSET2_INT_EN |
						LOW_OFFSET1_INT_EN |
						LOW_OFFSET0_INT_EN);
	}

	writel(temp, LVTSMONINT_0 + base);
}

static void enable_hw_reboot_interrupt(struct lvts_data *lvts_data, int tc_id)
{
	unsigned int temp;
	void __iomem *base;

	base = GET_BASE_ADDR(tc_id);

	/* Enable the interrupt of AP SW */
	temp = readl(LVTSMONINT_0 + base);

	if (lvts_data->enable_dump_log) {
		temp = temp | HIGH_OFFSET3_INT_EN |
				HIGH_OFFSET2_INT_EN |
				HIGH_OFFSET1_INT_EN |
				HIGH_OFFSET0_INT_EN;

		temp = temp | LOW_OFFSET3_INT_EN |
				LOW_OFFSET2_INT_EN |
				LOW_OFFSET1_INT_EN |
				LOW_OFFSET0_INT_EN;
	} else {
		temp = temp | STAGE3_INT_EN;
	}

	writel(temp, LVTSMONINT_0 + base);

	/* Clear the offset */
	temp = readl(LVTSPROTCTL_0 + base);
	writel(temp & ~PROTOFFSET, LVTSPROTCTL_0 + base);
}

static void set_tc_hw_reboot_threshold(struct lvts_data *lvts_data,
	int trip_point, int tc_id)
{
	struct tc_settings *tc = lvts_data->tc;
	unsigned int msr_raw, cur_msr_raw, temp, config, d_index, i;
	void __iomem *base;
	struct platform_ops *ops = &lvts_data->ops;

	base = GET_BASE_ADDR(tc_id);
	d_index = get_dominator_index(lvts_data, tc_id);

	disable_hw_reboot_interrupt(lvts_data, tc_id);

	temp = readl(LVTSPROTCTL_0 + base);
	if (d_index == ALL_SENSING_POINTS) {
		/* Maximum of 4 sensing points */
		config = (0x1 << 16);
		writel(config | temp, LVTSPROTCTL_0 + base);
		msr_raw = 0;
		for (i = 0; i < tc[tc_id].num_sensor; i++) {
			cur_msr_raw = ops->lvts_temp_to_raw(&(tc[tc_id].coeff), i, trip_point);
			if (msr_raw < cur_msr_raw)
				msr_raw = cur_msr_raw;
		}
	} else {
		/* Select protection sensor */
		config = ((d_index << 2) + 0x2) << 16;
		writel(config | temp, LVTSPROTCTL_0 + base);
		msr_raw = ops->lvts_temp_to_raw(&(tc[tc_id].coeff), d_index, trip_point);
	}

	if (lvts_data->enable_dump_log) {
		/* high offset INT */
		writel(msr_raw, LVTSOFFSETH_0 + base);

		/*
		 * lowoffset INT
		 * set a big msr_raw = 0xffff(very low temperature)
		 * to let lowoffset INT not be triggered
		 */
		writel(0xffff, LVTSOFFSETL_0 + base);
	} else {
		writel(msr_raw, LVTSPROTTC_0 + base);
	}

	enable_hw_reboot_interrupt(lvts_data, tc_id);
}

static void set_all_tc_hw_reboot(struct lvts_data *lvts_data)
{
	struct tc_settings *tc = lvts_data->tc;
	int i, trip_point;

	disable_all_sensing_points(lvts_data);
	lvts_wait_for_all_sensing_point_idle(lvts_data);
	for (i = 0; i < lvts_data->num_tc; i++) {
		trip_point = tc[i].hw_reboot_trip_point;

		if (tc[i].num_sensor == 0)
			continue;

		if (trip_point == DISABLE_THERMAL_HW_REBOOT) {
			disable_hw_reboot_interrupt(lvts_data, i);
			continue;
		}

		set_tc_hw_reboot_threshold(lvts_data, trip_point, i);
	}
	enable_all_sensing_points(lvts_data);
}

static void update_all_tc_hw_reboot_point(struct lvts_data *lvts_data,
	int trip_point)
{
	struct tc_settings *tc = lvts_data->tc;
	int i;

	for (i = 0; i < lvts_data->num_tc; i++)
		tc[i].hw_reboot_trip_point = trip_point;
}

static int soc_temp_lvts_set_trip_temp(struct thermal_zone_device *tz,
		int trip, int temp)
{
	struct soc_temp_tz *lvts_tz = (struct soc_temp_tz *)tz->devdata;
	struct lvts_data *lvts_data = lvts_tz->lvts_data;
	const struct thermal_trip *trip_points;

	int ret = 0;

	trip_points = lvts_data->tz_dev->trips;
	if (!trip_points)
		return -EINVAL;

	if (trip_points[trip].type != THERMAL_TRIP_CRITICAL || lvts_tz->id != 0)
		return 0;

	update_all_tc_hw_reboot_point(lvts_data, temp);
	set_all_tc_hw_reboot(lvts_data);

	ret = set_reboot_temperature(temp);

	return ret;
}

static const struct thermal_zone_device_ops soc_temp_lvts_ops = {
	.get_temp = soc_temp_lvts_read_temp,
	.set_trip_temp = soc_temp_lvts_set_trip_temp,
};

static int prepare_calibration_data(struct lvts_data *lvts_data)
{
	struct device *dev = lvts_data->dev;
	struct sensor_cal_data *cal_data = &lvts_data->cal_data;
	struct platform_ops *ops = &lvts_data->ops;
	struct tc_settings *tc = lvts_data->tc;
	int i;

	cal_data->count_r = devm_kcalloc(dev, lvts_data->num_sensor,
				sizeof(*cal_data->count_r), GFP_KERNEL);
	if (!cal_data->count_r)
		return -ENOMEM;

	cal_data->count_rc = devm_kcalloc(dev, lvts_data->num_sensor,
				sizeof(*cal_data->count_rc), GFP_KERNEL);
	if (!cal_data->count_rc)
		return -ENOMEM;

	if (ops->efuse_to_cal_data)
		ops->efuse_to_cal_data(lvts_data);

	if (ops->check_cal_data)
		ops->check_cal_data(lvts_data);

	dev_dbg(dev, "[lvts_cal] cali_mode = %d\n", cal_data->cali_mode);

	dev_dbg(dev, "[lvts_cal] num:g_count:g_count_rc ");

	for (i = 0; i < lvts_data->num_sensor; i++) {
		dev_dbg(dev, "%d:%d:%d ",
			i, cal_data->count_r[i], cal_data->count_rc[i]);
	}

	dev_dbg(dev, "[lvts_cal] num_tc:g_golden_temp");
	for (i = 0; i < lvts_data->num_tc; i++) {
		dev_dbg(dev, "%d:%d ",
			i, tc[i].coeff.golden_temp);
	}

	return 0;
}

static int get_calibration_data(struct lvts_data *lvts_data)
{
	struct device *dev = lvts_data->dev;
	char cell_name[8] = "e_data0";
	struct nvmem_cell *cell;
	u32 *buf;
	size_t len = 0;
	int i, j, index = 0, ret;

	lvts_data->efuse = devm_kcalloc(dev, lvts_data->num_efuse_addr,
				sizeof(*lvts_data->efuse), GFP_KERNEL);
	if (!lvts_data->efuse)
		return -ENOMEM;

	for (i = 0; i < lvts_data->num_efuse_block; i++) {
		cell_name[6] = '1' + i;
		cell = nvmem_cell_get(dev, cell_name);
		if (IS_ERR(cell)) {
			dev_err(dev, "Error: Failed to get nvmem cell %s\n",
				cell_name);
			return PTR_ERR(cell);
		}

		buf = (u32 *)nvmem_cell_read(cell, &len);
		nvmem_cell_put(cell);

		if (IS_ERR(buf))
			return PTR_ERR(buf);

		for (j = 0; j < (len / sizeof(u32)); j++) {
			if (index >= lvts_data->num_efuse_addr) {
				dev_err(dev, "Array efuse is going to overflow");
				kfree(buf);
				return -EINVAL;
			}

			lvts_data->efuse[index] = buf[j];
			index++;
		}

		kfree(buf);
	}

	ret = prepare_calibration_data(lvts_data);

	return ret;
}

static int of_update_lvts_data(struct lvts_data *lvts_data,
	struct platform_device *pdev)
{
	struct device *dev = lvts_data->dev;
	struct power_domain *domain;
	struct resource *res;
	struct platform_ops *ops = &lvts_data->ops;
	unsigned int i;
	int ret;
	int irq;

	if (!lvts_data->clock_gate_no_need) {
		lvts_data->clk = devm_clk_get(dev, "lvts_clk");
		if (IS_ERR(lvts_data->clk))
			return PTR_ERR(lvts_data->clk);

	}
	domain = devm_kcalloc(dev, lvts_data->num_domain, sizeof(*domain),
			GFP_KERNEL);
	if (!domain)
		return -ENOMEM;

	for (i = 0; i < lvts_data->num_domain; i++) {
		/* Get base address */
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(dev, "No IO resource, index %d\n", i);
			return -ENXIO;
		}

		domain[i].base = devm_ioremap_resource(dev, res);
		if (IS_ERR(domain[i].base)) {
			dev_err(dev, "Failed to remap io, index %d\n", i);
			return PTR_ERR(domain[i].base);
		}

		/* Get interrupt number */
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			dev_err(dev, "No IRQ resource at index %d\n", i);
			return -ENOENT;
		}
		dev_dbg(dev, "domain[%d] irq_num=%d\n", i, irq);
		domain[i].irq_num = irq;

		if (!lvts_data->reset_no_need) {
			/* Get reset control */
			domain[i].reset = devm_reset_control_get_by_index(dev, i);
			if (IS_ERR(domain[i].reset)) {
				dev_err(dev, "Failed to get, index %d\n", i);
				return PTR_ERR(domain[i].reset);
			}
		}
	}

	lvts_data->domain = domain;

	ret = get_calibration_data(lvts_data);
	if (ret)
		return ret;

	if (ops->update_coef_data)
		ops->update_coef_data(lvts_data);

	return 0;
}

static void tc_irq_handler(struct lvts_data *lvts_data, int tc_id)
{
	struct device *dev = lvts_data->dev;
	unsigned int ret = 0;
	void __iomem *base;
	int temp;

	temp = lvts_read_all_tc_temperature(lvts_data, true);

	base = GET_BASE_ADDR(tc_id);
	ret = readl(LVTSMONINTSTS_0 + base);
	/* Write back to clear interrupt status */
	writel(ret, LVTSMONINTSTS_0 + base);

	dev_info(dev, "[Thermal IRQ] LVTS thermal controller %d, LVTSMONINTSTS=0x%08x, T=%d\n",
		tc_id, ret, temp);

	orderly_poweroff(true);
}

static irqreturn_t irq_handler(int irq, void *dev_id)
{
	struct lvts_data *lvts_data = (struct lvts_data *) dev_id;
	struct device *dev = lvts_data->dev;
	struct tc_settings *tc = lvts_data->tc;
	unsigned int i;
	void __iomem *base;
	unsigned int reboot_tc = lvts_data->num_tc;

	if (!lvts_data->dump_wo_pause)
		disable_all_sensing_points(lvts_data);
	else {
		for (i = 0; i < lvts_data->num_tc; i++) {
			base = GET_BASE_ADDR(i);
			if (readl(LVTSMONINTSTS_0 + base) & THERMAL_PROTECTION_STAGE_3){
				reboot_tc = i;
				break;
			}
		}
		dev_dbg(dev, "%s : reboot_tc = 0x%x\n", __func__, reboot_tc);
	}

	for (i = 0; i < lvts_data->num_domain; i++) {
		base = lvts_data->domain[i].base;
		lvts_data->irq_bitmap[i] = readl(THERMINTST + base);
		dev_info(dev, "%s : THERMINTST = 0x%x\n", __func__,
			lvts_data->irq_bitmap[i]);
	}

	for (i = 0; i < lvts_data->num_tc; i++) {
		if ((lvts_data->irq_bitmap[tc[i].domain_index] & tc[i].irq_bit) == 0 &&
			lvts_data->irq_bitmap[tc[i].domain_index] != 0)
			tc_irq_handler(lvts_data, i);
	}

	return IRQ_HANDLED;
}

static int lvts_register_irq_handler(struct lvts_data *lvts_data)
{
	struct device *dev = lvts_data->dev;
	unsigned int i, *irq_bitmap;
	int ret;

	irq_bitmap = devm_kcalloc(dev, lvts_data->num_domain, sizeof(*irq_bitmap),
			GFP_KERNEL);
	if (!irq_bitmap)
		return -ENOMEM;

	lvts_data->irq_bitmap = irq_bitmap;

	for (i = 0; i < lvts_data->num_domain; i++) {
		if(lvts_data->ap_domain_no_irq)
			if (i == 0)
				continue;

		ret = devm_request_irq(dev, lvts_data->domain[i].irq_num,
			irq_handler, IRQF_TRIGGER_HIGH, "mtk_lvts", lvts_data);

		if (ret) {
			dev_err(dev,
				"Failed to register LVTS IRQ, ret %d, domain %d irq_num %d\n",
				ret, i, lvts_data->domain[i].irq_num);
			return ret;
		}
	}

	return 0;
}

static int lvts_register_thermal_zone(int id, struct lvts_data *lvts_data,
		struct thermal_zone_device **tzdev)
{
	struct device *dev = lvts_data->dev;
	struct soc_temp_tz *lvts_tz;
	int ret = 0;

	lvts_tz = devm_kzalloc(dev, sizeof(*lvts_tz), GFP_KERNEL);
	if (!lvts_tz)
		return -ENOMEM;

	lvts_tz->id = id;
	lvts_tz->lvts_data = lvts_data;

	*tzdev = devm_thermal_of_zone_register(dev, lvts_tz->id,
			lvts_tz, &soc_temp_lvts_ops);

	if (IS_ERR(*tzdev)) {
		ret = PTR_ERR(*tzdev);
		dev_err(dev,
			"Error: Failed to register lvts tz %d, ret = %d\n",
			lvts_tz->id, ret);
	}
	return ret;
}

static int lvts_register_thermal_zones(struct lvts_data *lvts_data)
{
	struct thermal_zone_device *tzdev;
	int i, j, s_index, ret = 0;

	ret = lvts_register_thermal_zone(0, lvts_data, &tzdev);
	if (ret)
		return ret;

	lvts_data->tz_dev = tzdev;

	for (i = 0; i < lvts_data->num_tc; i++) {
		for (j = 0; j < lvts_data->tc[i].num_sensor; j++) {
			if (lvts_data->tc[i].sensor_on_off[j] != SEN_ON)
				continue;

			s_index = lvts_data->tc[i].sensor_map[j];

			ret = lvts_register_thermal_zone(s_index + 1, lvts_data, &tzdev);
			if (ret)
				return ret;


		}
	}

	return ret;
}

static const struct of_device_id toprgu_of_match[] = {
	{ .compatible = "mediatek,mt6589-wdt" },
	{},
};

static int lvts_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lvts_data *lvts_data;
	int ret;
	struct device_node *np_toprgu;

	lvts_data = (struct lvts_data *) of_device_get_match_data(dev);
	if (!lvts_data)	{
		dev_err(dev, "Error: Failed to get lvts platform data\n");
		return -ENODATA;
	}

	lvts_data->dev = &pdev->dev;

	ret = of_update_lvts_data(lvts_data, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, lvts_data);

	for_each_matching_node(np_toprgu, toprgu_of_match) {
		dev_info(dev, "%s: compatible node found: %s\n",
			 __func__, np_toprgu->name);
		break;
	}

	lvts_data->toprgu_base = of_iomap(np_toprgu, 0);
	if (!lvts_data->toprgu_base)
		dev_err(dev, "toprgu iomap failed\n");

	ret = lvts_register_irq_handler(lvts_data);
	if (ret)
		return ret;

	ret = lvts_register_thermal_zones(lvts_data);
	if (ret)
		return ret;


	return 0;
}

static int lvts_remove(struct platform_device *pdev)
{
	return 0;
}

static void lvts_shutdown(struct platform_device *pdev)
{
	struct lvts_data *lvts_data;
	struct device *dev = &pdev->dev;
	unsigned int val = 0;

	lvts_data = (struct lvts_data *) dev_get_drvdata(dev);

	if (lvts_data->support_shutdown) {
		if (lvts_data->toprgu_base) {
			val = ioread32(lvts_data->toprgu_base + WDT_REQ_MODE);
			val &= ~(WDT_STATUS_MCU_THERMAL_RST);
			val |= WDT_REQ_MODE_KEY;
			iowrite32(val, lvts_data->toprgu_base + WDT_REQ_MODE);
		}
		dev_info(dev, "[Thermal/LVTS]%s(WDT_REQ:0x%x!)\n",
			__func__, ioread32(lvts_data->toprgu_base + WDT_REQ_MODE));
	}
}

static int lvts_suspend_noirq(struct device *dev)
{
	dev_info(dev, "[Thermal/LVTS]%s\n", __func__);

	return 0;
}

static int lvts_resume_noirq(struct device *dev)
{
	struct lvts_data *lvts_data;

	lvts_data = (struct lvts_data *) dev_get_drvdata(dev);
	dev_info(dev, "[Thermal/LVTS]%s\n", __func__);

	return 0;
}

/*==================================================
 * LVTS MT8196
 *==================================================
 */
enum mt8196_lvts_domain {
	MT8196_AP_DOMAIN,
	MT8196_MCU_DOMAIN,
	MT8196_APU_DOMAIN,
	MT8196_GPU_DOMAIN,
	MT8196_NUM_DOMAIN
};

enum mt8196_lvts_sensor_enum {
	MT8196_TS1_0,
	MT8196_TS1_1,
	MT8196_TS1_2,
	MT8196_TS1_3,
	MT8196_TS2_0,
	MT8196_TS2_1,
	MT8196_TS2_2,
	MT8196_TS2_3,
	MT8196_TS3_0,
	MT8196_TS3_1,
	MT8196_TS3_2,//10
	MT8196_TS3_3,
	MT8196_TS4_0,
	MT8196_TS4_1,
	MT8196_TS4_2,
	MT8196_TS4_3,
	MT8196_TS5_0,
	MT8196_TS5_1,
	MT8196_TS5_2,
	MT8196_TS5_3,
	MT8196_TS7_0,//20
	MT8196_TS7_1,
	MT8196_TS11_0,
	MT8196_TS11_1,
	MT8196_TS11_2,
	MT8196_TS11_3,//25
	MT8196_TS12_0,
	MT8196_TS12_1,
	MT8196_TS12_2,
	MT8196_TS12_3,
	MT8196_NUM_TS,
};



enum mt8196_lvts_controller_enum {
	MT8196_LVTS_MCU_CTRL0,
	MT8196_LVTS_MCU_CTRL1,
	MT8196_LVTS_MCU_CTRL2,
	MT8196_LVTS_MCU_CTRL3,
	MT8196_LVTS_AP_CTRL0,
	MT8196_LVTS_AP_CTRL1,
	MT8196_LVTS_APU_CTRL0,
	MT8196_LVTS_GPU_CTRL0,
	MT8196_LVTS_CTRL_NUM
};

static void mt8196_efuse_to_cal_data(struct lvts_data *lvts_data)
{
	struct sensor_cal_data *cal_data = &lvts_data->cal_data;
	struct tc_settings *tc = lvts_data->tc;
	int i = 0;

	cal_data->cali_mode = GET_CAL_DATA_BIT(0, 31);
	cal_data->golden_temp_ht = GET_CAL_DATA_BITMASK(0, 15, 8);
	cal_data->golden_temp = GET_CAL_DATA_BITMASK(0, 7, 0);

	for (i = 0; i < lvts_data->num_tc; i++)
		tc[i].coeff.golden_temp = cal_data->golden_temp;

	if (cal_data->cali_mode == 1) {
		for (i = 0; i < lvts_data->num_tc; i++) {
			if (tc[i].coeff.cali_mode == CALI_HT)
				tc[i].coeff.golden_temp = cal_data->golden_temp_ht;
		}
	}

	cal_data->count_r[MT8196_TS1_0] =  GET_CAL_DATA_BITMASK(1, 31, 16);
	cal_data->count_r[MT8196_TS1_1] =  GET_CAL_DATA_BITMASK(1, 15, 0);
	cal_data->count_r[MT8196_TS1_2] =  GET_CAL_DATA_BITMASK(2, 31, 16);
	cal_data->count_r[MT8196_TS1_3] =  GET_CAL_DATA_BITMASK(2, 15, 0);

	cal_data->count_r[MT8196_TS2_0] =  GET_CAL_DATA_BITMASK(3, 31, 16);
	cal_data->count_r[MT8196_TS2_1] =  GET_CAL_DATA_BITMASK(3, 15, 0);
	cal_data->count_r[MT8196_TS2_2] =  GET_CAL_DATA_BITMASK(4, 31, 16);
	cal_data->count_r[MT8196_TS2_3] =  GET_CAL_DATA_BITMASK(4, 15, 0);

	cal_data->count_r[MT8196_TS3_0] =  GET_CAL_DATA_BITMASK(5, 31, 16);
	cal_data->count_r[MT8196_TS3_1] =  GET_CAL_DATA_BITMASK(5, 15, 0);
	cal_data->count_r[MT8196_TS3_2] =  GET_CAL_DATA_BITMASK(6, 31, 16);
	cal_data->count_r[MT8196_TS3_3] =  GET_CAL_DATA_BITMASK(6, 15, 0);

	cal_data->count_r[MT8196_TS4_0] =  GET_CAL_DATA_BITMASK(7, 31, 16);
	cal_data->count_r[MT8196_TS4_1] =  GET_CAL_DATA_BITMASK(7, 15, 0);
	cal_data->count_r[MT8196_TS4_2] =  GET_CAL_DATA_BITMASK(8, 31, 16);
	cal_data->count_r[MT8196_TS4_3] =  GET_CAL_DATA_BITMASK(8, 15, 0);

	cal_data->count_r[MT8196_TS5_0] =  GET_CAL_DATA_BITMASK(9, 31, 16);
	cal_data->count_r[MT8196_TS5_1] =  GET_CAL_DATA_BITMASK(9, 15, 0);
	cal_data->count_r[MT8196_TS5_2] =  GET_CAL_DATA_BITMASK(10, 31, 16);
	cal_data->count_r[MT8196_TS5_3] =  GET_CAL_DATA_BITMASK(10, 15, 0);

	cal_data->count_r[MT8196_TS7_0]  =  GET_CAL_DATA_BITMASK(11, 31, 16);
	cal_data->count_r[MT8196_TS7_1]  =  GET_CAL_DATA_BITMASK(11, 15, 0);
	cal_data->count_r[MT8196_TS11_0] =  GET_CAL_DATA_BITMASK(12, 31, 16);
	cal_data->count_r[MT8196_TS11_1] =  GET_CAL_DATA_BITMASK(12, 15, 0);

	cal_data->count_r[MT8196_TS11_2] =  GET_CAL_DATA_BITMASK(13, 31, 16);
	cal_data->count_r[MT8196_TS11_3] =  GET_CAL_DATA_BITMASK(13, 15, 0);
	cal_data->count_r[MT8196_TS12_0] =  GET_CAL_DATA_BITMASK(14, 31, 16);
	cal_data->count_r[MT8196_TS12_1] =  GET_CAL_DATA_BITMASK(14, 15, 0);

	cal_data->count_r[MT8196_TS12_2] =  GET_CAL_DATA_BITMASK(15, 31, 16);
	cal_data->count_r[MT8196_TS12_3] =  GET_CAL_DATA_BITMASK(15, 15, 0);

	cal_data->count_rc[MT8196_LVTS_MCU_CTRL0] =  GET_CAL_DATA_BITMASK(16, 23, 0);
	cal_data->count_rc[MT8196_LVTS_MCU_CTRL1] =  GET_CAL_DATA_BITMASK(17, 23, 0);
	cal_data->count_rc[MT8196_LVTS_MCU_CTRL2] =  GET_CAL_DATA_BITMASK(18, 23, 0);
	cal_data->count_rc[MT8196_LVTS_MCU_CTRL3] =  GET_CAL_DATA_BITMASK(19, 23, 0);

	cal_data->count_rc[MT8196_LVTS_APU_CTRL0] =  GET_CAL_DATA_BITMASK(20, 23, 0);
	cal_data->count_rc[MT8196_LVTS_GPU_CTRL0] =  GET_CAL_DATA_BITMASK(21, 23, 0);

	cal_data->count_rc[MT8196_LVTS_AP_CTRL0] =  GET_CAL_DATA_BITMASK(22, 23, 0);
	cal_data->count_rc[MT8196_LVTS_AP_CTRL1] =  GET_CAL_DATA_BITMASK(23, 23, 0);

	if (!lvts_data->op_cali_support) {
		cal_data->op_cali[MT8196_LVTS_MCU_CTRL0] =  GET_CAL_DATA_BITMASK(16, 29, 24);
		cal_data->op_cali[MT8196_LVTS_MCU_CTRL1] =  GET_CAL_DATA_BITMASK(17, 29, 24);
		cal_data->op_cali[MT8196_LVTS_MCU_CTRL2] =  GET_CAL_DATA_BITMASK(18, 29, 24);
		cal_data->op_cali[MT8196_LVTS_MCU_CTRL3] =  GET_CAL_DATA_BITMASK(19, 29, 24);

		cal_data->op_cali[MT8196_LVTS_APU_CTRL0] =  GET_CAL_DATA_BITMASK(20, 29, 24);
		cal_data->op_cali[MT8196_LVTS_GPU_CTRL0] =  GET_CAL_DATA_BITMASK(21, 29, 24);

		cal_data->op_cali[MT8196_LVTS_AP_CTRL0] =  GET_CAL_DATA_BITMASK(22, 29, 24);
		cal_data->op_cali[MT8196_LVTS_AP_CTRL1] =  GET_CAL_DATA_BITMASK(23, 29, 24);
	}
}

static void mt8196_check_cal_data(struct lvts_data *lvts_data)
{
	struct device *dev = lvts_data->dev;
	struct sensor_cal_data *cal_data = &lvts_data->cal_data;
	struct tc_settings *tc = lvts_data->tc;
	int i;

	cal_data->use_fake_efuse = 1;

	if ((cal_data->golden_temp != 0) || (cal_data->golden_temp_ht != 0)) {
		cal_data->use_fake_efuse = 0;
	} else {
		for (i = 0; i < lvts_data->num_sensor; i++) {
			if (cal_data->count_r[i] != 0) {
				cal_data->use_fake_efuse = 0;
				break;
			}
		}
		if (cal_data->use_fake_efuse == 1) {
			for (i = 0; i < lvts_data->num_tc; i++) {
				if (cal_data->count_rc[i] != 0) {
					cal_data->use_fake_efuse = 0;
					break;
				}
			}
		}
	}

	if (cal_data->use_fake_efuse) {
		/* It means all efuse data are equal to 0 */
		dev_info(dev,
			"[lvts_cal] This sample is not calibrated, fake !!\n");
		for (i = 0; i < lvts_data->num_sensor; i++)
			cal_data->count_r[i] = cal_data->default_count_r;


		for (i = 0; i < lvts_data->num_tc; i++)
			cal_data->count_rc[i] = cal_data->default_count_rc;

		if (!lvts_data->op_cali_support)
			for (i = 0; i < lvts_data->num_tc; i++)
				cal_data->op_cali[i] = 0;

		for (i = 0; i < lvts_data->num_tc; i++) {
			if (tc[i].coeff.cali_mode == CALI_HT &&
				cal_data->cali_mode == 1)
				tc[i].coeff.golden_temp = cal_data->default_golden_temp_ht;
			else
				tc[i].coeff.golden_temp = cal_data->default_golden_temp;
		}
	}
}

#define COF_A_T_SLP_GLD_8196 391460
#define COF_A_COUNT_R_GLD_8196 34412
#define COF_A_CONST_OFS_8196 0
#define COF_A_OFS_8196 (COF_A_T_SLP_GLD_8196 - COF_A_CONST_OFS_8196)

static void mt8196_update_coef_data(struct lvts_data *lvts_data)
{
	struct sensor_cal_data *cal_data = &lvts_data->cal_data;
	struct tc_settings *tc = lvts_data->tc;
	unsigned int i, j, s_index;

	for (i = 0; i < lvts_data->num_tc; i++) {
		for  (j = 0; j < tc[i].num_sensor; j++) {
			if (tc[i].sensor_on_off[j] != SEN_ON)
				continue;

			s_index = tc[i].sensor_map[j];

			tc[i].coeff.a[j] = COF_A_OFS_8196 + (COF_A_CONST_OFS_8196 *
					cal_data->count_r[s_index] / COF_A_COUNT_R_GLD_8196);

			dev_dbg(lvts_data->dev, "%s tc[%d].coeff.a[%d]=%d\n", __func__,
				i, j, tc[i].coeff.a[j]);
		}
	}
}

static struct tc_settings mt8196_tc_settings[] = {
	[MT8196_LVTS_MCU_CTRL0] = {
		.domain_index = MT8196_MCU_DOMAIN,
		.addr_offset = 0x0,
		.num_sensor = 4,
		.sensor_map = {MT8196_TS1_0, MT8196_TS1_1, MT8196_TS1_2, MT8196_TS1_3},
		.sensor_on_off = {SEN_ON, SEN_ON, SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 1440, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 118800,
		.irq_bit = BIT(1),
		.coeff = {
			.cali_mode = CALI_HT,
		},
	},
	[MT8196_LVTS_MCU_CTRL1] = {
		.domain_index = MT8196_MCU_DOMAIN,
		.addr_offset = 0x100,
		.num_sensor = 4,
		.sensor_map = {MT8196_TS2_0, MT8196_TS2_1, MT8196_TS2_2, MT8196_TS2_3},
		.sensor_on_off = {SEN_ON, SEN_ON, SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 1440, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 118800,
		.irq_bit = BIT(2),
		.coeff = {
			.cali_mode = CALI_HT,
		},
	},
	[MT8196_LVTS_MCU_CTRL2] = {
		.domain_index = MT8196_MCU_DOMAIN,
		.addr_offset = 0x200,
		.num_sensor = 4,
		.sensor_map = {MT8196_TS3_0, MT8196_TS3_1, MT8196_TS3_2, MT8196_TS3_3},
		.sensor_on_off = {SEN_ON, SEN_ON, SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 1440, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 118800,
		.irq_bit = BIT(3),
		.coeff = {
			.cali_mode = CALI_NT,
		},
	},
	[MT8196_LVTS_MCU_CTRL3] = {
		.domain_index = MT8196_MCU_DOMAIN,
		.addr_offset = 0x300,
		.num_sensor = 4,
		.sensor_map = {MT8196_TS4_0, MT8196_TS4_1, MT8196_TS4_2, MT8196_TS4_3},
		.sensor_on_off = {SEN_ON, SEN_ON, SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 10, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 114200,
		.irq_bit = BIT(4),
		.coeff = {
			.cali_mode = CALI_NT,
		},
	},
	[MT8196_LVTS_AP_CTRL0] = {
		.domain_index = MT8196_AP_DOMAIN,
		.addr_offset = 0x0,
		.num_sensor = 4,
		.sensor_map = {MT8196_TS11_0, MT8196_TS11_1, MT8196_TS11_2, MT8196_TS11_3},
		.sensor_on_off = {SEN_ON, SEN_ON, SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 327670, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 118800,
		.irq_bit = BIT(1),
		.coeff = {
			.cali_mode = CALI_NT,
		},
	},
	[MT8196_LVTS_AP_CTRL1] = {
		.domain_index = MT8196_AP_DOMAIN,
		.addr_offset = 0x100,
		.num_sensor = 4,
		.sensor_map = {MT8196_TS12_0, MT8196_TS12_1, MT8196_TS12_2, MT8196_TS12_3},
		.sensor_on_off = {SEN_ON, SEN_ON, SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 327670, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 118800,
		.irq_bit = BIT(2),
		.coeff = {
			.cali_mode = CALI_NT,
		},
	},
	[MT8196_LVTS_APU_CTRL0] = {
		.domain_index = MT8196_APU_DOMAIN,
		.addr_offset = 0x0,
		.num_sensor = 4,
		.sensor_map = {MT8196_TS5_0, MT8196_TS5_1, MT8196_TS5_2, MT8196_TS5_3},
		.sensor_on_off = {SEN_ON, SEN_ON, SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 1950, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 118800,
		.irq_bit = BIT(1),
		.coeff = {
			.cali_mode = CALI_NT,
		},
	},
	[MT8196_LVTS_GPU_CTRL0] = {
		.domain_index = MT8196_GPU_DOMAIN,
		.addr_offset = 0x0,
		.num_sensor = 2,
		.sensor_map = {MT8196_TS7_0, MT8196_TS7_1},
		.sensor_on_off = {SEN_ON, SEN_ON},
		.tc_speed = SET_TC_SPEED_IN_US(10, 1230, 10, 10),
		.hw_filter = LVTS_FILTER_1,
		.dominator_sensing_point = ALL_SENSING_POINTS,
		.hw_reboot_trip_point = 118800,
		.irq_bit = BIT(1),
		.coeff = {
			.cali_mode = CALI_NT,
		},
	},
};

static struct lvts_data mt8196_lvts_data = {
	.num_domain = MT8196_NUM_DOMAIN,
	.num_tc = MT8196_LVTS_CTRL_NUM,
	.tc = mt8196_tc_settings,
	.num_sensor = MT8196_NUM_TS,
	.ops = {
		.efuse_to_cal_data = mt8196_efuse_to_cal_data,
		.lvts_temp_to_raw = lvts_temp_to_raw,
		.lvts_raw_to_temp = lvts_raw_to_temp,
		.check_cal_data = mt8196_check_cal_data,
		.update_coef_data = mt8196_update_coef_data,
	},
	.feature_bitmap = FEATURE_DEVICE_AUTO_RCK,
	.num_efuse_addr = 24,
	.num_efuse_block = 4,
	.cal_data = {
		.default_golden_temp = 60,
		.default_golden_temp_ht = 170,
		.default_count_r = 14437,
		.default_count_rc = 14778,
	},
	.init_done = false,
	.enable_dump_log = 0,
	.clock_gate_no_need = true,
	.reset_no_need = true,
	.spm_lvts = true,
	.op_cali_support = true,
	.is_tsfdc_n3e_ver = true,
	.dump_wo_pause = true,
	.support_shutdown = true,
	.gpu_power_ctrl_id = MT8196_LVTS_GPU_CTRL0,
	.ap_domain_no_irq = true,
};

/*==================================================
 * Support chips
 *==================================================
 */
static const struct dev_pm_ops lvts_pm_ops = {
	.suspend_noirq = lvts_suspend_noirq,
	.resume_noirq = lvts_resume_noirq,
};

static const struct of_device_id lvts_of_match[] = {
	{
		.compatible = "mediatek,mt8196-lvts",
		.data = (void *)&mt8196_lvts_data,
	},
	{
	},
};
MODULE_DEVICE_TABLE(of, lvts_of_match);
/*==================================================*/
static struct platform_driver soc_temp_lvts = {
	.probe = lvts_probe,
	.remove = lvts_remove,
	.shutdown = lvts_shutdown,
	.driver = {
		.name = "mtk-soc-temp-lvts-mt8196",
		.of_match_table = lvts_of_match,
		.pm = &lvts_pm_ops,
	},
};

module_platform_driver(soc_temp_lvts);
MODULE_AUTHOR("Yu-Chia Chang <ethan.chang@mediatek.com>");
MODULE_DESCRIPTION("Mediatek soc temperature driver");
MODULE_LICENSE("GPL");
