#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/mt-plat/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpueb/include/
#ccflags-y += -DDEBUG

subdir-ccflags-y += -Wno-error

obj-$(CONFIG_MALI_MTK) += mtk_gpueb.o

mtk_gpueb-y += \
    gpueb.o \
    gpueb_ipi.o \
    gpueb_logger.o \
    gpueb_plat_service.o \
    gpueb_reserved_mem.o \
    gpueb_gpumpu.o \
    gpueb_hwvoter_dbg.o \
    gpueb_debug.o \
    gpueb_timesync.o \
    ghpm_wrapper.o \
    ghpm_debug.o

mtk_ghpm-y := \
	ghpm.o \

mtk_ghpm_swwa-y := \
	ghpm_swwa.o \

obj-$(CONFIG_MALI_MTK) += mtk_ghpm.o
obj-$(CONFIG_MALI_MTK) += mtk_ghpm_swwa.o
