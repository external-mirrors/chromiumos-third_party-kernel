#
# Copyright (C) 2019 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See http://www.gnu.org/licenses/gpl-2.0.html for more details.
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpufreq/v2/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/ged/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpueb/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/mt-plat/

ccflags-y += \
	-Wno-unused-variable \
	-Wno-unused-function

mtk_gpufreq_wrapper-y := \
	gpufreq_v2.o \
	gpufreq_history.o \
	gpufreq_debug.o

mtk_gpufreq_mt8196-y := \
	gpufreq_history_mt8196.o \
	gpuppm.o \
	gpufreq_mt8196.o

obj-$(CONFIG_MALI_MTK) += mtk_gpufreq_wrapper.o
obj-$(CONFIG_MALI_MTK) += mtk_gpufreq_mt8196.o
