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

#define PCI_VENDOR_EIDETICOM 0x1de5

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephen Bates <stephen@eideticom.com");
MODULE_DESCRIPTION("A P2PMEM driver for simple PCIe End Points (EPs)");

static uint pci_bar;
module_param(pci_bar, uint, S_IRUGO);

static struct class *p2pmem_class;
static DEFINE_IDA(p2pmem_ida);

static struct pci_device_id p2pmem_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_EIDETICOM, 0x1000) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, p2pmem_pci_id_table);

struct p2pmem_dev {
	struct device dev;
	struct pci_dev *pdev;
	int id;
};

static struct p2pmem_dev *to_p2pmem(struct device *dev)
{
	return container_of(dev, struct p2pmem_dev, dev);
}

static void p2pmem_release(struct device *dev)
{
	struct p2pmem_dev *p = to_p2pmem(dev);

	kfree(p);
}

static struct p2pmem_dev *p2pmem_create(struct pci_dev *pdev)
{
	struct p2pmem_dev *p;
	int err;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	p->pdev = pdev;

	device_initialize(&p->dev);
	p->dev.class = p2pmem_class;
	p->dev.parent = &pdev->dev;
	p->dev.release = p2pmem_release;

	p->id = ida_simple_get(&p2pmem_ida, 0, 0, GFP_KERNEL);
	if (p->id < 0) {
		err = p->id;
		goto out_free;
	}

	dev_set_name(&p->dev, "p2pmem%d", p->id);

	err = device_add(&p->dev);
	if (err)
		goto out_ida;

	dev_info(&p->dev, "registered");

	return p;

out_ida:
	ida_simple_remove(&p2pmem_ida, p->id);
out_free:
	kfree(p);
	return ERR_PTR(err);
}

void p2pmem_destroy(struct p2pmem_dev *p)
{
	dev_info(&p->dev, "unregistered");
	device_del(&p->dev);
	ida_simple_remove(&p2pmem_ida, p->id);
	put_device(&p->dev);
}

static int p2pmem_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct p2pmem_dev *p;
	int err = 0;

	if (pci_enable_device_mem(pdev) < 0) {
		dev_err(&pdev->dev, "unable to enable device!\n");
		goto out;
	}

	p = p2pmem_create(pdev);
	if (IS_ERR(p))
		goto out_disable_device;

	pci_set_drvdata(pdev, p);
	pci_p2pmem_add_resource(pdev, pci_bar, 0);

	return 0;

out_disable_device:
	pci_disable_device(pdev);
out:
	return err;
}

static void p2pmem_pci_remove(struct pci_dev *pdev)
{
	struct p2pmem_dev *p = pci_get_drvdata(pdev);

	p2pmem_destroy(p);
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

	p2pmem_class = class_create(THIS_MODULE, "p2pmem");
	if (IS_ERR(p2pmem_class))
		return PTR_ERR(p2pmem_class);

	rc = pci_register_driver(&p2pmem_pci_driver);
	if (rc)
		goto err_class;

	pr_info(KBUILD_MODNAME ": module loaded\n");

	return 0;
err_class:
	class_destroy(p2pmem_class);
	return rc;
}

static void __exit p2pmem_pci_cleanup(void)
{
	pci_unregister_driver(&p2pmem_pci_driver);
	class_destroy(p2pmem_class);
	pr_info(KBUILD_MODNAME ": module unloaded\n");
}

module_init(p2pmem_pci_init);
module_exit(p2pmem_pci_cleanup);
