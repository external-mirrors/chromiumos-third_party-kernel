# SPDX-License-Identifier: GPL-2.0

obj-y				+= platform/

cam-objs			+= cam-device.o \
					cam-output.o \
					cam-ioctl.o \
					cam-graph.o \
					cam-namespace.o \
					cam-entity.o \
					cam-buffer.o \
					cam-ringbuffer.o \
					cam-syncfile.o \
					cam-pipeline.o

ifeq ($(CONFIG_TRACEPOINTS),y)
	cam-objs		+= cam-trace.o
endif
obj-$(CONFIG_CAM)		+= cam.o

cam-ioctl-test-objs		+= cam-ioctl.o
cam-namespace-test-objs		+= cam-namespace.o
cam-graph-test-objs		+= cam-graph.o
cam-ringbuffer-test-objs	+= cam-ringbuffer.o
obj-$(CONFIG_CAM_KUNIT_TESTS)	+= cam-ioctl-test.o \
					cam-namespace-test.o \
					cam-graph-test.o \
					cam-ringbuffer-test.o
