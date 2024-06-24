obj-$(CONFIG_SECURITY_CHROMIUMOS) := chromiumos_lsm.o

chromiumos_lsm-y := inode_mark.o lsm.o securityfs.o utils.o

obj-$(CONFIG_ALT_SYSCALL_CHROMIUMOS) += alt-syscall.o
ccflags-$(CONFIG_ALT_SYSCALL_CHROMIUMOS) += -Wno-cast-function-type-strict
