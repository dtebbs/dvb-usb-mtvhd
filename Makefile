
# [*] USB_ ids into mtvhd-usb-ids.h
# [*] Get mtvhd-compat.h
# [*] -include mtvhd-compat.h

obj-m += \
    dvb-usb-mtvhd.o \
    dvb-usb-asv5211.o \
    # d-input-test.o

d-input-test-objs := \
    input-test.o

dvb-usb-mtvhd-objs := \
    mtvhd.o mtvhd-v1.o mtvhd-xor.o \
    mtvhd-stream.o mtvhd-v2.o \
    des.o mtvhd-des-gnulib.o

dvb-usb-asv5211-objs := \
    asv5211.o

# mtvhd-des-kernel.o

# realsrctree = /usr/src/linux-source-$(shell uname -r)
realsrctree = $(obj)/linux-source

EXTRA_CFLAGS += \
    -include $(obj)/mtvhd-compat.h \
    -include $(obj)/mtvhd-usb-ids.h \
    -I $(realsrctree)/include/uapi/linux \
    -I $(realsrctree)/drivers/media/dvb-core \
    -I $(realsrctree)/drivers/media/dvb-frontends \
    -I $(realsrctree)/drivers/media/usb/dvb-usb

    # -I $(srctree)/include/media \
    # -I $(srctree)/drivers/media/dvb/dvb-core \
    # -I $(srctree)/drivers/media/dvb/frontends \
    # -I $(srctree)/drivers/media/dvb/dvb-usb \



dot-config := 1
all:
	make -C /lib/modules/$(shell uname -r)/build SUBDIRS=$(PWD) V=1 modules
#	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) V=1 modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean


input-test :
	-sudo rmmod d-input-test
	sudo insmod d-input-test.ko
	echo waiting ...
	sleep 3
	sudo rmmod d-input-test.ko
