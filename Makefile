
# [*] USB_ ids into mtvhd-usb-ids.h
# [*] Get mtvhd-compat.h
# [*] -include mtvhd-compat.h

obj-m += \
    dvb-usb-mtvhd.o \
    dvb-usb-asv5211.o

dvb-usb-mtvhd-objs := \
    mtvhd.o mtvhd-v1.o mtvhd-xor.o \
    mtvhd-stream.o mtvhd-v2.o \
    des.o mtvhd-des-gnulib.o

dvb-usb-asv5211-objs := \
    asv5211.o

# mtvhd-des-kernel.o

realsrctree = /usr/src/linux-$(shell uname -r)

EXTRA_CFLAGS += \
    -include $(obj)/mtvhd-compat.h \
    -include $(obj)/mtvhd-usb-ids.h \
    -I $(srctree)/drivers/media/dvb/dvb-core \
    -I $(srctree)/drivers/media/dvb/frontends \
    -I $(srctree)/drivers/media/dvb/dvb-usb \
    -I $(realsrctree)/drivers/media/dvb/dvb-core \
    -I $(realsrctree)/drivers/media/dvb/frontends \
    -I $(realsrctree)/drivers/media/dvb/dvb-usb


dot-config := 1
all:
	make -C /lib/modules/$(shell uname -r)/build SUBDIRS=$(PWD) V=1 modules
#	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) V=1 modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
