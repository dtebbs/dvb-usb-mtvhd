
# [*] USB_ ids into mtvhd-usb-ids.h
# [*] Get mtvhd-compat.h
# [*] -include mtvhd-compat.h

obj-m += \
    dvb-usb-mtvhd.o \
    dvb-usb-asv5211.o

dvb-usb-mtvhd-objs := \
    mtvhd.o mtvhd-des-kernel.o mtvhd-v1.o mtvhd-xor.o \
    des.o mtvhd-des-gnulib.o mtvhd-stream.o mtvhd-v2.o

EXTRA_CFLAGS += \
    -include $(obj)/mtvhd-compat.h \
    -include $(obj)/mtvhd-usb-ids.h \
    -I $(srctree)/drivers/media/dvb/dvb-core \
    -I $(srctree)/drivers/media/dvb/frontends \
    -I $(srctree)/drivers/media/dvb/dvb-usb

dot-config := 1
all:
	make -C /lib/modules/$(shell uname -r)/build SUBDIRS=$(PWD) V=1 modules
#	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) V=1 modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
