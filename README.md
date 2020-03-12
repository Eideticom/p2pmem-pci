# P2PMEM PCIe Linux Driver

## Disclaimer

This driver exposes p2pmem to userspace without taking appropriate
safety measures to ensure it is used correctly. If you do certain
things with the pointers obtained by mmapping the /dev/p2pmemX exposed
by this driver bad things will probably happen. Consider yourself
warned.

## Introduction

This is a standlone PCIe driver that ties into the [p2pmem][1]
framework. It can be used for any PCIe end-point devices that have
registered one or more PCIe BAR(s) with the p2pdma framework (e.g. [NVM
Express CMBs][2]).

## Build and Install

To build the module as a .ko for manually installation run:

```
make KDIR=/path/to/kernel/source
```

Note the resultant kernel module is called p2pmem_pci.ko. To install
the module in the current kernel's module tree run:

```
make install
```

Note that for this to work you either need to have module signing set
up or turned off on your machine. Also this code will only work for >=
4.20.x kernels. Please look for the largest tag that is less than or
equal to your kernel version and use that tag.

## Usage

Once this module has been inserted you should see a /dev/p2pmemX for
each of the p2pmem regions in your system. You can then use mmap() to
obtain virtual address pointers backed by memory on the PCIe BAR(s)
associated with /dev/p2pmemX. You can then pass these pointers into
library functions like write() and read() *as long as you use
O_DIRECT*.

An example of how to use /dev/p2pmemX is via [p2pmem-test][3] which
also has more information on setting up p2pdma enabled kernels and a
p2pdma capable system.

[1]: https://www.kernel.org/doc/html/latest/driver-api/pci/p2pdma.html
[2]: https://www.flashmemorysummit.com/English/Collaterals/Proceedings/2018/20180809_NVMF-301-1_Maier.pdf
[3]: https://github.com/sbates130272/p2pmem-test
