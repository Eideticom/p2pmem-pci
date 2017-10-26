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

default: modules
.PHONY: default

obj-m += p2pmem_pci.o

%::
	$(MAKE) -C $(KERNEL_SOURCES) M=$$PWD $@

p2pmem-pci.ko:

install: modules_install
.PHONY: install
