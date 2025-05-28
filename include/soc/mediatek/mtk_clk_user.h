/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#ifndef __MTK_CLK_USER_H
#define __MTK_CLK_USER_H

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/types.h>

extern const struct clk_ops mtk_mux_user_ops;

struct mtk_mux_user {
	int id;
	const char *name;
	const char *target_name;
	unsigned long rate;
	const struct clk_ops *ops;
	unsigned int flags;
	u8 target_id;
	unsigned long undo_rate;
};

struct mtk_clk_user {
	struct clk_hw hw;
	struct clk_hw *target_hw;
	spinlock_t *lock;
	unsigned int flags;
};

struct dfs_ops {
	int (*set_opp)(const char *name, unsigned long rate);
	int (*get_opp)(const char *name);
	unsigned long (*get_rate)(const char *name);
};

int mtk_clk_mux_register_user_clks(const struct mtk_mux_user *clks, int num,
		spinlock_t *lock, struct clk_hw_onecell_data *clk_data, struct device *dev);
void mtk_clk_mux_register_callback(const struct dfs_ops *ops);

#endif /* __MTK_CLK_USER_H */
