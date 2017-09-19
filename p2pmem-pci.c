/*
 * P2PMEM PCI EP Device Driver
 * Copyright (c) 2017, Eideticom
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Copyright (C) 2017 Eideitcom
 */

#include <linux/module.h>
#include <linux/p2pmem.h>
#include <linux/pci.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephen Bates <stephen@eideticom.com");
MODULE_DESCRIPTION("A P2PMEM driver for simple PCIe End Points (EPs)");

static uint pci_bar;
module_param(pci_bar, uint, S_IRUGO);

static struct pci_device_id p2pmem_pci_id_table[] = {
	{ PCI_DEVICE(0x10ee, 0x0888) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, p2pmem_pci_id_table);

struct p2pmem_pci_device {
	struct device *dev;
	struct p2pmem_dev *p2pmem;
};

/*
 * Copied from https://github.com/sbates130272/linux-p2pmem/\
 * commit/19ea30476314359c7ae22c48434edb4269dda976
 */
static int init_p2pmem(struct p2pmem_pci_device *p2pmem_pci)
{
	struct p2pmem_dev *p;
	int rc;
	struct pci_dev *pdev = to_pci_dev(p2pmem_pci->dev);
	struct resource *res = &pdev->resource[pci_bar];

	p = p2pmem_create(&pdev->dev);
	if (IS_ERR(p))
		return PTR_ERR(p);

	rc = p2pmem_add_resource(p, res);
	if (rc) {
		p2pmem_unregister(p);
		return rc;
	}
	p2pmem_pci->p2pmem = p;

	return 0;
}

static int p2pmem_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct p2pmem_pci_device *p2pmem_pci;
	int err = 0;

	if (pci_enable_device_mem(pdev) < 0) {
		dev_err(&pdev->dev, "unable to enable device!\n");
		goto out;
	}

	p2pmem_pci = kzalloc(sizeof(*p2pmem_pci), GFP_KERNEL);
	if (unlikely(!p2pmem_pci)) {
		err = -ENOMEM;
		goto out_disable_device;
	}

	p2pmem_pci->dev = get_device(&pdev->dev);
	pci_set_drvdata(pdev, p2pmem_pci);
	init_p2pmem(p2pmem_pci);

	return 0;

out_disable_device:
	pci_disable_device(pdev);
out:
	return err;
}

static void p2pmem_pci_remove(struct pci_dev *pdev)
{
	struct p2pmem_pci_device *p2pmem_pci = pci_get_drvdata(pdev);
	p2pmem_unregister(p2pmem_pci->p2pmem);
}

static struct pci_driver p2pmem_pci_driver = {
	.name = "p2pmem_pci",
	.id_table = p2pmem_pci_id_table,
	.probe = p2pmem_pci_probe,
	.remove = p2pmem_pci_remove,
};

static int __init p2pmem_pci_init(void)
{
	int rc;

	rc = pci_register_driver(&p2pmem_pci_driver);
	if (rc)
		return rc;

	pr_info("p2pmem-pci: module loaded\n");
	return 0;
}

static void __exit p2pmem_pci_cleanup(void)
{
	pci_unregister_driver(&p2pmem_pci_driver);
	pr_info("p2pmem-pci: module unloaded\n");
}

module_init(p2pmem_pci_init);
module_exit(p2pmem_pci_cleanup);
