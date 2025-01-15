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

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/midgard/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpu_pdma/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/midgard/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/ged/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/midgard/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpueb/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/midgard/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpufreq

ifneq ($(wildcard $(srctree)/$(src)/gpu_pdma_mt8196.c),)
mtk_gpu_pdma_mt8196-y := gpu_pdma_mt8196.o
obj-y += mtk_gpu_pdma_mt8196.o
endif
