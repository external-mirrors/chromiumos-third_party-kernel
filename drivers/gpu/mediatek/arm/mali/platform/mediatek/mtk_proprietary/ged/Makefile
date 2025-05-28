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

GED_TOP = $(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/ged
MTK_TOP = $(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary
GPU_TOP = $(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali

subdir-ccflags-y += -Wno-error

ccflags-y += \
	-Wno-unused-variable \
	-Wno-unused-function

# ccflags-y += -DMTK_CPUFREQ
# ccflags-y += -DGED_KPI_DEBUG
# ccflags-y += -DGED_DEBUG_LOG
ccflags-y += -DMTK_GPUFREQ_V2
ccflags-y += -DMTK_GED_KPI
ccflags-y += -DMET_USER_EVENT_SUPPORT -DMTK_GPU_DVFS
ccflags-y += -DGED_DVFS_DEBUG_BUF
ccflags-y += -DMTK_GPU_BM_2
ccflags-y += -DCONFIG_MTK_GPU_FW_IDLE
ccflags-y += -DCONFIG_MTK_GPU_APO_SUPPORT
ccflags-y += -DCONFIG_MTK_GPU_POWER_ON_OFF_TEST
ccflags-y += -DMTK_GPU_SLC_POLICY

ccflags-y += \
	-DENABLE_COMMON_DVFS \
	-DENABLE_TIMER_BACKUP \
	-DGED_DCS_POLICY \
	-DMTK_GPU_EB_SUPPORT

ccflags-y += \
	-I$(GED_TOP)/include/ \
	-I$(GPU_TOP)/ \
	-I$(GPU_TOP)/hal/ \
	-I$(MTK_TOP)/ \
	-I$(MTK_TOP)/gpufreq/v2/include/ \
	-I$(MTK_TOP)/gpu_bm/

ifeq ($(CONFIG_DEBUG_FS),y)
ccflags-y += -DGED_DEBUG_FS
ged-y += src/ged_debugFS.o
endif

ged-y += \
	src/ged_gpufreq_v2.o \
	src/ged_dcs.o \
	src/ged.o \
	src/ged_base.o \
	src/ged_main.o \
	src/ged_sysfs.o \
	src/ged_hal.o \
	src/ged_log.o \
	src/ged_bridge.o \
	src/ged_monitor_3D_fence.o \
	src/ged_notify_sw_vsync.o \
	src/ged_hashtable.o \
	src/ged_thread.o \
	src/ged_ge.o \
	src/ged_dvfs.o \
	src/ged_gpu_tuner.o \
	src/ged_kpi.o \
	src/ged_eb.o \
	src/ged_gpu_bm.o \
	src/ged_gpu_slc.o \
	src/ged_mali_event.o \
	src/ged_clk_rate_trace.o

ged-$(CONFIG_SYNC_FILE) += src/ged_sync.o

obj-$(CONFIG_MALI_MTK) += ged.o
