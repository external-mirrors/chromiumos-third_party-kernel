# SPDX-License-Identifier: GPL-2.0

obj-y				+= platform/

isp-objs			+= isp-device.o \
					isp-output.o \
					isp-ioctl.o \
					isp-graph.o \
					isp-namespace.o \
					isp-entity.o \
					isp-buffer.o \
					isp-ringbuffer.o \
					isp-syncfile.o \
					isp-pipeline.o

ifeq ($(CONFIG_TRACEPOINTS),y)
	isp-objs		+= isp-trace.o
endif
obj-$(CONFIG_ISP)		+= isp.o

isp-ioctl-test-objs		+= isp-ioctl.o
isp-namespace-test-objs		+= isp-namespace.o
isp-graph-test-objs		+= isp-graph.o
isp-ringbuffer-test-objs	+= isp-ringbuffer.o
obj-$(CONFIG_ISP_KUNIT_TESTS)	+= isp-ioctl-test.o \
					isp-namespace-test.o \
					isp-graph-test.o \
					isp-ringbuffer-test.o
