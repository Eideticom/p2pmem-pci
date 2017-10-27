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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pfn_t.h>

#define PCI_VENDOR_EIDETICOM 0x1de5
#define PCI_VENDOR_MICROSEMI 0x11f8
#define PCI_MTRAMON_DEV_ID   0xf117

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephen Bates <stephen@eideticom.com");
MODULE_DESCRIPTION("A P2PMEM driver for simple PCIe End Points (EPs)");

static int max_devices = 16;
module_param(max_devices, int, 0444);
MODULE_PARM_DESC(max_devices, "Maximum number of char devices");

static struct class *p2pmem_class;
static DEFINE_IDA(p2pmem_ida);
static dev_t p2pmem_devt;

static struct pci_device_id p2pmem_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_EIDETICOM, 0x1000), .driver_data = 0 },
	{ PCI_DEVICE(PCI_VENDOR_MICROSEMI,
		     PCI_MTRAMON_DEV_ID), .driver_data = 4 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, p2pmem_pci_id_table);

struct p2pmem_dev {
	struct device dev;
	struct pci_dev *pdev;
	int id;
	struct cdev cdev;
	bool mtramon;
};

static struct p2pmem_dev *to_p2pmem(struct device *dev)
{
	return container_of(dev, struct p2pmem_dev, dev);
}

struct p2pmem_vma {
	struct p2pmem_dev *p2pmem_dev;
	atomic_t mmap_count;
	size_t nr_pages;

	/* Protects the used_pages array */
	struct mutex mutex;
	struct page *used_pages[];
};

static void p2pmem_vma_open(struct vm_area_struct *vma)
{
	struct p2pmem_vma *pv = vma->vm_private_data;

	atomic_inc(&pv->mmap_count);
}

static void p2pmem_vma_free_pages(struct vm_area_struct *vma)
{
	int i;
	struct p2pmem_vma *pv = vma->vm_private_data;

	mutex_lock(&pv->mutex);

	for (i = 0; i < pv->nr_pages; i++) {
		if (pv->used_pages[i]) {
			pci_free_p2pmem(pv->p2pmem_dev->pdev,
					page_to_virt(pv->used_pages[i]),
					PAGE_SIZE);
			pv->used_pages[i] = NULL;
		}
	}

	mutex_unlock(&pv->mutex);
}

static void p2pmem_vma_close(struct vm_area_struct *vma)
{
	struct p2pmem_vma *pv = vma->vm_private_data;

	if (!atomic_dec_and_test(&pv->mmap_count))
		return;

	p2pmem_vma_free_pages(vma);

	dev_dbg(&pv->p2pmem_dev->dev, "vma close");
	kfree(pv);
}

static int p2pmem_vma_fault(struct vm_fault *vmf)
{
	struct p2pmem_vma *pv = vmf->vma->vm_private_data;
	unsigned int pg_idx;
	struct page *pg;
	pfn_t pfn;
	int rc;

	pg_idx = (vmf->address - vmf->vma->vm_start) / PAGE_SIZE;

	mutex_lock(&pv->mutex);

	if (pv->used_pages[pg_idx])
		pg = pv->used_pages[pg_idx];
	else
		pg = virt_to_page(pci_alloc_p2pmem(pv->p2pmem_dev->pdev,
						   PAGE_SIZE));

	if (!pg) {
		mutex_unlock(&pv->mutex);
		return VM_FAULT_OOM;
	}

	pv->used_pages[pg_idx] = pg;

	pfn = phys_to_pfn_t(page_to_phys(pg), PFN_DEV | PFN_MAP);
	rc = vm_insert_mixed(vmf->vma, vmf->address, pfn);

	mutex_unlock(&pv->mutex);

	if (rc == -ENOMEM)
		return VM_FAULT_OOM;
	if (rc < 0 && rc != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

const struct vm_operations_struct p2pmem_vmops = {
	.open = p2pmem_vma_open,
	.close = p2pmem_vma_close,
	.fault = p2pmem_vma_fault,
};

static int p2pmem_open(struct inode *inode, struct file *filp)
{
	struct p2pmem_dev *p;

	p = container_of(inode->i_cdev, struct p2pmem_dev, cdev);
	filp->private_data = p;

	return 0;
}

static int p2pmem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct p2pmem_dev *p = filp->private_data;
	struct p2pmem_vma *pv;
	size_t nr_pages = (vma->vm_end - vma->vm_start) / PAGE_SIZE;

	if ((vma->vm_flags & VM_MAYSHARE) != VM_MAYSHARE) {
		dev_warn(&p->dev, "mmap failed: can't create private mapping\n");
		return -EINVAL;
	}

	dev_dbg(&p->dev, "Allocating mmap with %zd pages.\n", nr_pages);

	pv = kzalloc(sizeof(*pv) + sizeof(pv->used_pages[0]) * nr_pages,
		     GFP_KERNEL);
	if (!pv)
		return -ENOMEM;

	mutex_init(&pv->mutex);
	pv->nr_pages = nr_pages;
	pv->p2pmem_dev = p;
	atomic_set(&pv->mmap_count, 1);

	vma->vm_private_data = pv;
	vma->vm_ops = &p2pmem_vmops;
	vma->vm_flags |= VM_MIXEDMAP;

	return 0;
}

static const struct file_operations p2pmem_fops = {
	.owner = THIS_MODULE,
	.open = p2pmem_open,
	.mmap = p2pmem_mmap,
};

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

	pci_p2pmem_add_resource(pdev, id->driver_data, 0);

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

static void ugly_mtramon_hack_init(void)
{
	struct pci_dev *pdev = NULL;
	struct p2pmem_dev *p;
	int err;

	while ((pdev = pci_get_device(PCI_VENDOR_MICROSEMI,
				      PCI_MTRAMON_DEV_ID,
				      pdev))) {
		// If there's no driver it can be handled by the regular
		//  pci driver case
		if (!pdev->driver)
			continue;

		if (!pdev->p2p_pool) {
			err = pci_p2pmem_add_resource(pdev, 4, 0);
			if (err) {
				dev_err(&pdev->dev,
					"unable to add p2p resource");
				continue;
			}
		}

		p = p2pmem_create(pdev);
		if (!p)
			continue;

		p->mtramon = true;
	}
}

static void ugly_mtramon_hack_deinit(void)
{
	struct class_dev_iter iter;
	struct device *dev;
	struct p2pmem_dev *p;

	class_dev_iter_init(&iter, p2pmem_class, NULL, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		p = to_p2pmem(dev);
		if (p->mtramon)
			p2pmem_destroy(p);
	}
	class_dev_iter_exit(&iter);
}

static int __init p2pmem_pci_init(void)
{
	int rc;

	p2pmem_class = class_create(THIS_MODULE, "p2pmem");
	if (IS_ERR(p2pmem_class))
		return PTR_ERR(p2pmem_class);

	rc = alloc_chrdev_region(&p2pmem_devt, 0, max_devices, "p2pmem");
	if (rc)
		goto err_class;

	ugly_mtramon_hack_init();

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
	ugly_mtramon_hack_deinit();
	unregister_chrdev_region(p2pmem_devt, max_devices);
	class_destroy(p2pmem_class);
	pr_info(KBUILD_MODNAME ": module unloaded\n");
}

module_init(p2pmem_pci_init);
module_exit(p2pmem_pci_cleanup);
