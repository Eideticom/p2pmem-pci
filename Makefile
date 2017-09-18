#
# By default, the build is done against the running linux kernel source.
# To build against a different kernel source tree, set SYSSRC:
#
#    make KDIR=/path/to/kernel/source

ifdef KDIR
 KERNEL_SOURCES	 = $(KDIR)
else
 KERNEL_UNAME	:= $(shell uname -r)
 KERNEL_SOURCES	 = /lib/modules/$(KERNEL_UNAME)/build
endif

obj-m += p2pmem-pci.o

all:
	make -C $(KERNEL_SOURCES) M=$(PWD) modules

clean:
	make -C $(KERNEL_SOURCES) M=$(PWD) clean
