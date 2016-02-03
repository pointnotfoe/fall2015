#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include <linux/io.h>
#include <asm/uaccess.h>

#include <linux/dma-mapping.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <linux/list.h>
#include <linux/wait.h>

#include <linux/cdev.h>
#include <linux/kfifo.h>

#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/signal.h>

#include <linux/sched.h>
#include <linux/mm.h>

#include "vga_ioctl.h"

/* Standard module information, edit as appropriate */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Matthews");
MODULE_DESCRIPTION ("zedboard axi vga driver");

#define DRIVER_NAME "vga"


//--frame buffer size (portion of memory reserved for holding complete bit-mapped image sent to monitor)
int FB_SIZE = 4*640*480;


/*
 * Main driver data structure
 */
struct vga_local {
   
    int major;
    int minor;

	unsigned long mem_start; //--physical address of VGA controller
	unsigned long mem_end;
	void __iomem *base_addr; //--virtual address of VGA controller

	void *fb_virt; //--virtual framebuffer address
	dma_addr_t fb_phys; //--physical framebuffer address in DMA format
	
	struct dev *dev;
	struct cdev *cdev; 
};

struct vga_local *vga; //--declare vga struct


/* 
 * Open function, called when userspace program calls open()
 */
static int vga_open(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 * Close function, called when userspace program calls close()
 */
static int vga_release(struct inode *inode, struct file *filp)
{
	return 0;
}


//--mmap function; tells VGA where frambuffer address is located; maps files into memory; creates VMA to do the mapping 
static int vga_mmap(struct file *filp, struct vm_area_struct *vma) //--vm_area_struct creates the VMA structure
{

	unsigned long pfn = dma_to_pfn(vga->dev, vga->fb_phys); //--getting page frame number
	unsigned long off = vma->vm_pgoff; //--getting offset from vm_pgoff is offset within file


	vma->vm_flags |= VM_RESERVED; //--tell memory management not to swap out VMA
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot); //--sets appropriate flags for noncached page tables 

	//--remap_pfn_range functions, maps new page tables to map range of physical addresses; vma is virtual area to which page range is being mapped; address to which mapping
	//--begins; the page frame number plus the offset, dimension in bytes of area being mapped; the protection required
	if (remap_pfn_range(vma, vma->vm_start, pfn+off, vma->vm_end - vma->vm_start, vma->vm_page_prot)){
	return -EAGAIN;
	}

	return 0;
	
}


/*
 * ioctl function, called when userspace program calls ioctl()
 */

//IOCTL function is a device specific system call; use this for reading and writing to vga; in order to communicate to device we need iowrite and ioread//driver is in kernel space

static long vga_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	return 0;

}

/*
 * File operations struct 
 * - informs kernel which functions should be called when userspace prorgams
 *   call different functions (ie. open, close, ioctl etc.)
 */
static struct file_operations vga_fops = {
	.open           = vga_open,
	.release        = vga_release,
	.mmap           = vga_mmap, //--include map in operations
	.unlocked_ioctl = vga_ioctl,
};

/*
 * Probe function (part of platform driver API)
 */
static int __devinit vga_probe(struct platform_device *pdev)
{
	struct resource *r_mem; /* IO mem resources */
	struct device *dev = &pdev->dev;
	

	int rc = 0;
	dev_t  devno;
	int err;
		
	/* dev_info is a logging function part of the platform driver API */
	dev_info(dev, "Device Tree Probing\n");

	/* Get iospace for the device */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		dev_err(dev, "invalid address\n");
		return -ENODEV;
	}
	
	/* Allocate space for driver data structure
	 * note the use of kmalloc - malloc (and all other C library functions) is
	 * unavailable in kernel code */
	vga = (struct vga_local *) kmalloc(sizeof(struct vga_local), GFP_KERNEL);
	if (!vga) {
		dev_err(dev, "Cound not allocate vga device\n");
		return -ENOMEM;
	}
	
	dev_set_drvdata(dev, vga);
	
	
	vga->mem_start = r_mem->start;
	vga->mem_end = r_mem->end;

	if (!request_mem_region(vga->mem_start,
				vga->mem_end - vga->mem_start + 1,
				DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %p\n",
			(void *)vga->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	/* Allocate I/O memory */
	vga->base_addr = ioremap(vga->mem_start, vga->mem_end - vga->mem_start + 1);
	if (!vga->base_addr) {
		dev_err(dev, "vga: Could not allocate iomem\n");
		rc = -EIO;
		goto error2;
	}

	
	//--DMA is a hardware block that allows direct access to RAM independent of CPU
	vga->fb_virt = dma_alloc_coherent(dev,FB_SIZE, &vga->fb_phys, GFP_KERNEL);//--allocate coherent mapping into virtual address of frame buffer using DMA; 
	//--dma_alloc_coherent returns pointer to the allocated memory 
 	
     
     dev_info(dev, "Registering character device\n");
 
    if ((alloc_chrdev_region(&devno, 0, 1, "vga")) < 0)  {
        goto error2; //change it to error4
	}

	/* Fill in driver data structure */
    vga->major = MAJOR(devno);
    vga->minor = MINOR(devno);
    		    
	dev_info(dev, "Initializing character device\n");
    		     	
    vga->cdev = cdev_alloc();
	vga->cdev->owner = THIS_MODULE;
	vga->cdev->ops =  &vga_fops;
	err = cdev_add (vga->cdev, devno, 1);

	/* Print driver info (addresses, major and minor num) */
	dev_info(dev,"vga at 0x%08x mapped to 0x%08x\nMajor: %d, Minor %d\n",
		(unsigned int __force)vga->mem_start,
		(unsigned int __force)vga->base_addr,
		vga->major, vga->minor);

	iowrite32(vga->fb_phys, vga->base_addr);//-- write the framebuffer address into the base address of VGA
	dev_info(dev, "frame buffer address: 0x%08x", vga->fb_phys); //--print out frame buffer address
	return 0;

/* Error handling for probe function
 * - this is one of very few cases where goto statements are a good idea
 * - when an error happens that prevents the driver from continuing to
 *   register/allocate resources, we need to undo any previous allocations
 *   that succeeded (in the reverse order)
 */


error2:
	release_mem_region(vga->mem_start, vga->mem_end - vga->mem_start + 1);
error1:
	kfree(vga);
	dev_set_drvdata(dev, NULL);
	return rc;
}

/*
 * Remove function (part of platform driver API)
 */
static int __devexit vga_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vga_local *vga = dev_get_drvdata(dev);


	release_mem_region(vga->mem_start, vga->mem_end - vga->mem_start + 1);
	
	
	dma_free_coherent(dev, FB_SIZE, &vga -> fb_phys, GFP_KERNEL);//--freeing DMA

	cdev_del(vga->cdev);
    	unregister_chrdev_region(MKDEV(vga->major, vga->minor), 1);
	
	
	kfree(vga);
	dev_set_drvdata(dev, NULL);
	return 0;
}

/*
 * Compatiblity string for matching driver to hardware
 */
#ifdef CONFIG_OF
static struct of_device_id vga_of_match[] __devinitdata = {
	/* This must match the compatible string in device tree source */
	{ .compatible = "ensc351-vga", }, 
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, vga_of_match);
#else
# define vga_of_match
#endif


/*
 * Platform driver struct
 * - used by platform driver API for device tree probing
 */
static struct platform_driver vga_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= vga_of_match,
	},
	.probe		= vga_probe,
	.remove		= __devexit_p(vga_remove),
};

/*
 * Driver initialization function
 */
static int __init vga_init(void)
{
	printk("<1>Hello module world.\n");
	return platform_driver_register(&vga_driver);
}

/*
 * Driver exit function
 */
static void __exit vga_exit(void)
{
	platform_driver_unregister(&vga_driver);
	printk(KERN_ALERT "Goodbye module world.\n");
}

/*
 * Register init and exit functions
 */
module_init(vga_init);
module_exit(vga_exit);

