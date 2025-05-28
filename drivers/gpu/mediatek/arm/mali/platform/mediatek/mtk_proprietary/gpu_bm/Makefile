#
# Copyright (C) 2018 MediaTek Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See http://www.gnu.org/licenses/gpl-2.0.html for more details.
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/ged/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpufreq/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sspm/v2/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/qos/

#ccflags-y += -DDEBUG

obj-$(CONFIG_MALI_MTK) += mtk_gpu_qos.o
mtk_gpu_qos-y := gpu_bm.o
