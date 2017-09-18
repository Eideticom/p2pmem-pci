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
#include <linux/pci.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephen Bates <stephen@eideticom.com");
MODULE_DESCRIPTION("A P2PMEM driver for simple PCIe End Points (EPs)");

static struct pci_device_id p2pmem_pci_id_table[] = {
	{ PCI_DEVICE(0x11f8, 0xf118) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, p2pmem_pci_id_table);

static int p2pmem_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	return 0;
}

static void p2pmem_pci_remove(struct pci_dev *pdev)
{
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
	pr_info("p2pmem: module unloaded\n");
}

module_init(p2pmem_pci_init);
module_exit(p2pmem_pci_cleanup);
