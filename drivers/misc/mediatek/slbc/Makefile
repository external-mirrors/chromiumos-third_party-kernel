# SPDX-License-Identifier: (GPL-2.0 OR MIT)

ccflags-y += \
	-I$(srctree)/drivers/misc/mediatek/include \
	-I$(srctree)/drivers/misc/mediatek/include/mt-plat \
	-I$(srctree)/drivers/misc/mediatek/sspm \
	-I$(srctree)/drivers/firmware/arm_scmi/ \
	-I$(srctree)/drivers/dma-buf/heaps \
	-I$(srctree)/drivers/misc/mediatek/sspm/v3 \
	-I$(srctree)/include \
	-I$(srctree)/$(src) \

obj-${CONFIG_MTK_SLBC} += mtk_slbc.o
mtk_slbc-y += slbc.o

obj-${CONFIG_MTK_SLBC_MT8196} += slbc_mt8196.o
obj-${CONFIG_MTK_SLBC_IPI} += slbc_ipi.o

