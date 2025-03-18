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
GPU_TOP = $(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary

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
#ccflags-y += -DMTK_GPU_BM_2
ccflags-y += -DCONFIG_MTK_GPU_FW_IDLE
ccflags-y += -DCONFIG_MTK_GPU_APO_SUPPORT
ccflags-y += -DCONFIG_MTK_GPU_POWER_ON_OFF_TEST
ccflags-y += -DMTK_GPU_SLC_POLICY



ifeq ($(CONFIG_MTK_GPU_MT6768_SUPPORT)$(CONFIG_MTK_GPU_MT6761_SUPPORT)$(CONFIG_MTK_GPU_MT6893_SUPPORT)$(CONFIG_MTK_GPU_MT6877_SUPPORT),)
ccflags-y += -DMTK_GPU_EB_SUPPORT
endif

ifeq ($(CONFIG_MTK_ENABLE_GMO), y)
ccflags-y += -DGED_BUFFER_LOG_DISABLE
endif

ccflags-y += -DENABLE_COMMON_DVFS \
             -DENABLE_TIMER_BACKUP

ifeq ($(CONFIG_MTK_FTRACE_DEFAULT_ENABLE),y)
ccflags-y += -DENABLE_GED_SYSTRACE_UTIL
endif

ccflags-y += -DGED_DCS_POLICY

ccflags-y += \
		-I$(DEVICE_MODULES_PATH)/include/ \
		-I$(MTK_TOP)/include/ \
		-I$(GED_TOP)/include/ \
		-I$(GPU_TOP)/hal/ \
		-I$(GPU_TOP)/ \
		-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lcm/inc/ \
		-I$(GPU_TOP)/gpufreq/ \
		-I$(GPU_TOP)/gpufreq/v1/include/ \
		-I$(GPU_TOP)/gpufreq/v2/include/ \
		-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/fpsgo_v3 \
		-I$(VIDEOX_TOP)/ \
		-I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/ \
		-I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpueb/include/ \
		-I$(DEVICE_MODULES_PATH)/drivers/dma-buf/heaps/

ccflags-y += \
		-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cmdq/v3

ccflags-y += -I$(GPU_TOP)/gpueb

ccflags-y += \
		-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sspm/ \
		-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sspm/v2 \
		-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/qos/ \
		-I$(GPU_TOP)/gpu_bm/ \
		-I$(GPU_TOP)/ged/include

ifeq ($(CONFIG_DEBUG_FS),y)
ccflags-y += -DGED_DEBUG_FS
ged-y += \
	src/ged_debugFS.o
endif

ged-y += \
	src/ged_gpufreq_v2.o \
	src/ged_dcs.o

ged-y += \
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

obj-y += ged.o
