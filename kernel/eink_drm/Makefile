# Out-of-tree build for the eink_drm kernel module

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m += eink_drm.o

eink_drm-y := eink_drm_drv.o \
	      protocol/ite8951_usb.o \
	      drm/eink_drm_kms.o

ccflags-y := -I$(src)

default:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
	@test -s eink_drm.ko || (echo "eink_drm.ko missing or empty — build failed" >&2; exit 1)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: default clean