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
#include <linux/pci-p2pdma.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pfn_t.h>

#define PCI_VENDOR_EIDETICOM 0x1de5
#define PCI_DEVICE_IOMEM 0x1000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Maier <andrew.maier@eideticom.com>");
MODULE_DESCRIPTION("A P2PMEM driver for simple PCIe End Points (EPs) to allow mmap into userspace");

static int max_devices = 16;
module_param(max_devices, int, 0444);
MODULE_PARM_DESC(max_devices, "Maximum number of char devices");

static struct class *p2pmem_class;
static DEFINE_IDA(p2pmem_ida);
static dev_t p2pmem_devt;

static struct pci_device_id p2pmem_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_EIDETICOM, PCI_DEVICE_IOMEM),
		.driver_data = 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, p2pmem_pci_id_table);

struct p2pmem_dev {
	struct device dev;
	struct pci_dev *pdev;
	int id;
	struct cdev cdev;
};

static struct p2pmem_dev *to_p2pmem(struct device *dev)
{
	return container_of(dev, struct p2pmem_dev, dev);
}

static int p2pmem_open(struct inode *inode, struct file *filp)
{
	struct p2pmem_dev *p;

	p = container_of(inode->i_cdev, struct p2pmem_dev, cdev);
	filp->private_data = p;
	//pci_p2pdma_file_open(p->pdev, filp);

	return 0;
}

static int p2pmem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct p2pmem_dev *p = filp->private_data;

	return pci_mmap_p2pmem(p->pdev, vma);
}

static const struct file_operations p2pmem_fops = {
	.owner = THIS_MODULE,
	.open = p2pmem_open,
	.mmap = p2pmem_mmap,
};

static int p2pmem_test_page_mappings(struct p2pmem_dev *p)
{
	void *addr;
	int err = 0;
	struct page *page;
	struct pci_bus_region bus_region;
	struct resource res;
	phys_addr_t pa;

	addr = pci_alloc_p2pmem(p->pdev, PAGE_SIZE);
	if (!addr)
		return -ENOMEM;

	page = virt_to_page(addr);
	if (!is_zone_device_page(page)) {
		dev_err(&p->dev,
			"ERROR: kernel virt_to_page does not point to a ZONE_DEVICE page!");
		err = -EFAULT;
		goto out;
	}

	bus_region.start = pci_p2pmem_virt_to_bus(p->pdev, addr);
	bus_region.end = bus_region.start + PAGE_SIZE;

	pcibios_bus_to_resource(p->pdev->bus, &res, &bus_region);

	pa = page_to_phys(page);
	if (pa != res.start) {
		dev_err(&p->dev,
			"ERROR: page_to_phys does not map to the BAR address!"
			"  %pa[p] != %pa[p]", &pa, &res.start);
		err = -EFAULT;
		goto out;
	}

	pa = virt_to_phys(addr);
	if (pa != res.start) {
		dev_err(&p->dev,
			"ERROR: virt_to_phys does not map to the BAR address!"
			"  %pa[p] != %pa[p]", &pa, &res.start);
		err = -EFAULT;
		goto out;
	}

	if (page_to_virt(page) != addr) {
		dev_err(&p->dev,
			"ERROR: page_to_virt does not map to the correct address!");
		err = -EFAULT;
		goto out;
	}

out:
	if (err == 0)
		dev_info(&p->dev, "kernel page mappings seem sane.");

	pci_free_p2pmem(p->pdev, addr, PAGE_SIZE);
	return err;
}

static int p2pmem_test_p2p_access(struct p2pmem_dev *p)
{
	u32 *addr;
	const u32 test_value = 0x11223344;
	int err = 0;

	addr = pci_alloc_p2pmem(p->pdev, PAGE_SIZE);
	if (!addr)
		return -ENOMEM;

	WRITE_ONCE(addr[0], 0);
	if (READ_ONCE(addr[0]) != 0) {
		err = -EFAULT;
		goto out;
	}

	WRITE_ONCE(addr[0], test_value);
	if (READ_ONCE(addr[0]) != test_value) {
		err = -EFAULT;
		goto out;
	}

out:
	if (err == 0)
		dev_info(&p->dev, "kernel can access p2p memory.");
	else
		dev_err(&p->dev, "ERROR: kernel can't access p2p memory!");

	pci_free_p2pmem(p->pdev, addr, PAGE_SIZE);
	return err;
}

static int p2pmem_test(struct p2pmem_dev *p)
{
	int err;

	err = p2pmem_test_page_mappings(p);
	if (err)
		return err;

	return p2pmem_test_p2p_access(p);
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
	p->dev.devt = MKDEV(MAJOR(p2pmem_devt), p->id);

	cdev_init(&p->cdev, &p2pmem_fops);
	p->cdev.owner = THIS_MODULE;

	err = cdev_device_add(&p->cdev, &p->dev);
	if (err)
		goto out_ida;

	dev_info(&p->dev, "registered");

	p2pmem_test(p);

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
	cdev_device_del(&p->cdev, &p->dev);
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

	err = pci_p2pdma_add_resource(pdev, 0, 0, 0);
	if (err) {
		dev_err(&pdev->dev, "unable to add p2p resource");
		goto out_disable_device;
	}

	pci_p2pmem_publish(pdev, true);

	p = p2pmem_create(pdev);
	if (IS_ERR(p))
		goto out_disable_device;

	pci_set_drvdata(pdev, p);

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

	p2pmem_class = class_create(THIS_MODULE, "p2pmem_device");
	if (IS_ERR(p2pmem_class))
		return PTR_ERR(p2pmem_class);

	rc = alloc_chrdev_region(&p2pmem_devt, 0, max_devices, "p2pmem");
	if (rc)
		goto err_class;

	rc = pci_register_driver(&p2pmem_pci_driver);
	if (rc)
		goto err_chdev;

	pr_info(KBUILD_MODNAME ": module loaded\n");

	return 0;
err_chdev:
	unregister_chrdev_region(p2pmem_devt, max_devices);
err_class:
	class_destroy(p2pmem_class);
	return rc;
}

static void __exit p2pmem_pci_cleanup(void)
{
	pci_unregister_driver(&p2pmem_pci_driver);
	unregister_chrdev_region(p2pmem_devt, max_devices);
	class_destroy(p2pmem_class);
	pr_info(KBUILD_MODNAME ": module unloaded\n");
}

late_initcall(p2pmem_pci_init);
module_exit(p2pmem_pci_cleanup);
