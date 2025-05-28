# SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
#
# (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU license.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
#

ccflags-y += \
        -I$(srctree)/drivers/gpu/mediatek/arm/mali/include \
        -I$(srctree)/drivers/dma-buf/heaps \
        -I$(srctree)/drivers/misc/mediatek/trusted_mem/public \
        -I$(srctree)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpufreq \
        -I$(srctree)/drivers/gpu/mediatek/arm/mali/platform/$(MALI_PLATFORM_DIR)/mtk_proprietary/gpueb/include

subdir-ccflags-y += $(ccflags-y)

ifeq ($(CONFIG_MALI_MTK_PROTECTED_MEMORY_ALLOCATOR), y)
obj-$(CONFIG_MALI_MTK) += protected_memory_allocator.o
endif
