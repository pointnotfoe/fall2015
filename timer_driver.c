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

#include "timer_ioctl.h"

/* Standard module information, edit as appropriate */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Matthews");
MODULE_DESCRIPTION ("zedboard axi timer driver");

#define DRIVER_NAME "timer"


//structures
static struct file_operations timer_fops;
struct timer_local  *timer;
static struct fasync_struct *async_queue;

//functions
static irq_handler_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int timer_fasync(int fd, struct file *filp, int mode);


/*
 * Main driver data structure
 */
struct timer_local {
    int major;
    int minor;
	unsigned int irq;
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
		
	struct cdev	*cdev;    
};



//IRQ handler function//
static irq_handler_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	//function is called when interrupt is recieved
	printk(KERN_ALERT "Hardware Interrupt!\n");

	//clear interrupt, reads control reg and writes this value	
	iowrite32(ioread32(timer->base_addr + CONTROL_REG), timer->base_addr + CONTROL_REG);
	
	if (async_queue) {
	printk ("signal async_queue");
       // signal the interested processes when data arrives 
        kill_fasync(&async_queue, SIGIO, POLL_IN);
    	}
	
	return IRQ_HANDLED;
}


static int timer_fasync(int fd, struct file *filp, int mode) {  //whenever interrupt is generated, notifies user space, by sending SIGIO signal; fasync is used to notify user space process 

	return fasync_helper(fd, filp, mode, &async_queue);

}



/* 
 * Open function, called when userspace program calls open()
 */
static int timer_open(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 * Close function, called when userspace program calls close()
 */
static int timer_release(struct inode *inode, struct file *filp)
{
	// Remove the file from the list of asynchronously notified filp's
	timer_fasync(-1, filp, 0);
	return 0;
}

/*
 * ioctl function, called when userspace program calls ioctl()
 */

//IOCTL function is a device specific system call; use this for reading and writing to timer; in order to communicate to device we need iowrite and ioread//driver is in kernel space

static long timer_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
struct timer_ioctl_data timer_data; 

	/* printf (along with other C library functions) is not available in
	 * kernel code, but printk is almost identical */
	//printk(KERN_ALERT "Starting IOCTL... \n");

	switch (cmd) { //switch funtion does whatever is true first
		case TIMER_READ_REG: //reading from timer register

			if (copy_from_user(&timer_data, (void *)arg, sizeof(timer_data))) {   
                    //we need to copy from user space to access it// copying from user space the data in address of arg //to the address of timer_data (value in timer) in kernel
				printk(KERN_ALERT "***Unsuccessful transfer of ioctl argument...\n");    
				return -EFAULT;
			}

			
			timer_data.data = ioread32(timer->base_addr+timer_data.offset); 
			

   //user loads timer, and we need to read this value; access device using ioread reading the timer value from memory and storing; this value is found in the base_addr+timer_data.offset; offset is defined by user??// in order to refer to this value, store it in data element of timer_data.data
			
			if (copy_to_user((void *)arg, &timer_data, sizeof(timer_data))) {        //copying to user space the data in timer_data (value in timer)
				printk(KERN_ALERT "***Unsuccessful transfer of ioctl argument...\n");   //from the address of arg  in kernel
				return -EFAULT;
			}
			
			break;

		

		case TIMER_WRITE_REG:  //writing to timer register
			
			if (copy_from_user(&timer_data, (void *)arg, sizeof(timer_data))) {       //copying from user space the data in address of arg
				printk(KERN_ALERT "***Unsuccessful transfer of ioctl argument...\n");   //to the address of timer_data (value in timer) in kernel
				return -EFAULT;
			}
			
			iowrite32(timer_data.data, timer->base_addr+timer_data.offset); //iowrite adds new information, updates curent value in timer
			
			break;

			
		default:
		printk(KERN_ALERT "***Invalid ioctl command...\n");
		return -EINVAL;

	}

	return 0;

}

/*
 * File operations struct 
 * - informs kernel which functions should be called when userspace prorgams
 *   call different functions (ie. open, close, ioctl etc.)
 */
static struct file_operations timer_fops = {
	.open           = timer_open,
	.release        = timer_release,
	.fasync         = timer_fasync,
	.unlocked_ioctl = timer_ioctl,
};

/*
 * Probe function (part of platform driver API)
 */
static int __devinit timer_probe(struct platform_device *pdev)
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
	timer = (struct timer_local *) kmalloc(sizeof(struct timer_local), GFP_KERNEL);
	if (!timer) {
		dev_err(dev, "Cound not allocate timer device\n");
		return -ENOMEM;
	}
	
	dev_set_drvdata(dev, timer);
	
	
	timer->mem_start = r_mem->start;
	timer->mem_end = r_mem->end;

	if (!request_mem_region(timer->mem_start,
				timer->mem_end - timer->mem_start + 1,
				DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %p\n",
			(void *)timer->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	/* Allocate I/O memory */
	timer->base_addr = ioremap(timer->mem_start, timer->mem_end - timer->mem_start + 1);
	if (!timer->base_addr) {
		dev_err(dev, "timer: Could not allocate iomem\n");
		rc = -EIO;
		goto error2;
	}

	// Request IRQ
	timer->irq = platform_get_irq(pdev, 0);
	rc = request_irq(timer->irq, (void *)timer_interrupt, 0, "timer_driver", dev);
	if (rc) {
		dev_err(dev, "can't get assigned irq %i\n", timer->irq);
		goto error3;
	} else {
		dev_info(dev, "assigned irq number %i\n", timer->irq);
	}


 	dev_info(dev, "Registering character device\n");
 
    if ((alloc_chrdev_region(&devno, 0, 1, "timer")) < 0)  {
        goto error4; //change it to error4
	}

	/* Fill in driver data structure */
    timer->major = MAJOR(devno);
    timer->minor = MINOR(devno);
    		    
	dev_info(dev, "Initializing character device\n");
    		     	
    timer->cdev = cdev_alloc();
	timer->cdev->owner = THIS_MODULE;
	timer->cdev->ops =  &timer_fops;
	err = cdev_add (timer->cdev, devno, 1);

	/* Print driver info (addresses, major and minor num) */
	dev_info(dev,"timer at 0x%08x mapped to 0x%08x\nMajor: %d, Minor %d\n",
		(unsigned int __force)timer->mem_start,
		(unsigned int __force)timer->base_addr,
		timer->major, timer->minor);
	return 0;

/* Error handling for probe function
 * - this is one of very few cases where goto statements are a good idea
 * - when an error happens that prevents the driver from continuing to
 *   register/allocate resources, we need to undo any previous allocations
 *   that succeeded (in the reverse order)
 */
error4: // Undo 'request_irq()'
	free_irq(timer->irq, dev);

error3: // Undo 'ioremap()'
	iounmap((void *)(timer->base_addr));

error2:
	release_mem_region(timer->mem_start, timer->mem_end - timer->mem_start + 1);
error1:
	kfree(timer);
	dev_set_drvdata(dev, NULL);
	return rc;
}

/*
 * Remove function (part of platform driver API)
 */
static int __devexit timer_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct timer_local *timer = dev_get_drvdata(dev);

	free_irq(timer->irq, dev);//freeing irq

	release_mem_region(timer->mem_start, timer->mem_end - timer->mem_start + 1);
	
	cdev_del(timer->cdev);
    unregister_chrdev_region(MKDEV(timer->major, timer->minor), 1);
	
	kfree(timer);
	dev_set_drvdata(dev, NULL);
	return 0;
}

/*
 * Compatiblity string for matching driver to hardware
 */
#ifdef CONFIG_OF
static struct of_device_id timer_of_match[] __devinitdata = {
	/* This must match the compatible string in device tree source */
	{ .compatible = "ensc351-timer", }, 
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, timer_of_match);
#else
# define timer_of_match
#endif


/*
 * Platform driver struct
 * - used by platform driver API for device tree probing
 */
static struct platform_driver timer_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= timer_of_match,
	},
	.probe		= timer_probe,
	.remove		= __devexit_p(timer_remove),
};

/*
 * Driver initialization function
 */
static int __init timer_init(void)
{
	printk("<1>Hello module world.\n");
	return platform_driver_register(&timer_driver);
}

/*
 * Driver exit function
 */
static void __exit timer_exit(void)
{
	platform_driver_unregister(&timer_driver);
	printk(KERN_ALERT "Goodbye module world.\n");
}

/*
 * Register init and exit functions
 */
module_init(timer_init);
module_exit(timer_exit);

