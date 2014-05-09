# This comes from the zio Makefile

LINUX ?= /lib/modules/$(shell uname -r)/build

mcuio-y := core.o bus.o sysfs.o

obj-$(CONFIG_MCUIO) += mcuio.o mcuio-hc-drv.o mcuio-hc-dev.o mcuio-soft-hc.o
obj-$(CONFIG_MCUIO_LDISC_HC) += mcuio-hc-ldisc.o
obj-$(CONFIG_MCUIO_IRQ_CTRL) += mcuio-irq-ctrl.o
