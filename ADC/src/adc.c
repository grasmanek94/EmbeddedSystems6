#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <asm/uaccess.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/irqs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Elviro & Rafal");
MODULE_DESCRIPTION("That's a kernel module wich handles ADC conversion");

#define DEVICE_NAME  "ES6_ADC"
#define ADC_NUMCHANNELS (3)
#define bool char
#define true  (1)
#define false (0)
#define SUCCESS  (0)

#define	ADCLK_CTRL			LPC32XX_CLKPWR_ADC_CLK_CTRL
#define	ADCLK_CTRL1			LPC32XX_CLKPWR_ADC_CLK_CTRL_1
#define	ADC_SELECT			io_p2v(LPC32XX_ADC_BASE + 0x04)
#define	ADC_CTRL			io_p2v(LPC32XX_ADC_BASE + 0x08)
#define ADC_VALUE       	io_p2v(LPC32XX_ADC_BASE + 0x48)
#define SIC2_ATR        	io_p2v(LPC32XX_SIC2_BASE + 0x10)

#define ADCLK_CTRL1_MASK          (0x01ff)
#define ADC_SELECT_START_MASK     (0x30)
#define ADC_SELECT_SHIFT          (4)
#define ADC_SELECT_RESET_MASK     (0x03c0)
#define ADC_SELECT_SET_MASK       (0x0280)
#define ADC_CTRL_MASK             (0x4)
#define ADC_CTRL_AD_START_MASK    (0x2)
#define ADC_VALUE_MASK            (0x3FF)
#define SIC2_EDGE_ACTIVATE        (0x800000)

#define MAX_BUFFER   (256)
#define NULL_BYTE_ENDING (1)

struct MessageData
{
    int length;
    int channel;
    char buffer[MAX_BUFFER];
};

static unsigned char adc_channel = 0;
static int           adc_values[ADC_NUMCHANNELS] = {0, 0, 0};
static struct        task_struct* current_task = NULL;

static irqreturn_t  adc_interrupt (int irq, void * dev_id);
static irqreturn_t  gp_interrupt  (int irq, void * dev_id);
struct semaphore channel_conversion;

dev_t          deviceP;
struct cdev    cDevices;

void cleanup_adc_module(void);
int init_adc_module (void);
static int dev_release (struct inode * inode, struct file * file);
static int dev_open (struct inode * inode, struct file * file);
static ssize_t dev_read (struct file * file, char __user * buf, size_t length, loff_t * f_pos);
static irqreturn_t gp_interrupt(int irq, void * dev_id);
static irqreturn_t adc_interrupt (int irq, void * dev_id);
static void adc_init (void);
static void adc_exit (void);

static struct file_operations fops =
{
   .owner = THIS_MODULE,
   .open = dev_open,
   .read = dev_read,
   .release = dev_release,
};

static void adc_init (void)
{
	unsigned long data;
  
    data = ioread32(ADCLK_CTRL);
    data |= LPC32XX_CLKPWR_ADC32CLKCTRL_CLK_EN;
    iowrite32 (data, ADCLK_CTRL);

    data = ioread32 (ADCLK_CTRL1);
    data &= ~ADCLK_CTRL1_MASK;
    iowrite32 (data, ADCLK_CTRL1);

    data = ioread32(ADC_SELECT);
    data &= ~ADC_SELECT_RESET_MASK;
    data |=  ADC_SELECT_SET_MASK;
    iowrite32 (data, ADC_SELECT);

	data = ioread32(ADC_CTRL);
	data |= ADC_CTRL_MASK;
	iowrite32(data, ADC_CTRL);

    data = ioread32(SIC2_ATR);
	data |= SIC2_EDGE_ACTIVATE;
	iowrite32(data, SIC2_ATR);
    
    if (request_irq (IRQ_LPC32XX_TS_IRQ, adc_interrupt, IRQF_DISABLED, DEVICE_NAME "_CONVERT", NULL) != 0)
    {
        printk(KERN_ALERT DEVICE_NAME ": ADC IRQ request failed\n");
    }

    if (request_irq (IRQ_LPC32XX_GPI_01, gp_interrupt, IRQF_DISABLED, DEVICE_NAME "_EINT0", NULL) != 0)
    {
        printk (KERN_ALERT DEVICE_NAME ": GP IRQ request failed\n");
    }
}

static void adc_start (unsigned char channel)
{
	unsigned long data;

	if (channel >= ADC_NUMCHANNELS)
    {
        channel = 0;
    }

	data = ioread32 (ADC_SELECT);

	iowrite32((data & ~ADC_SELECT_START_MASK) | ((channel << ADC_SELECT_SHIFT) & ADC_SELECT_START_MASK), ADC_SELECT);

	adc_channel = channel;
    printk(KERN_DEBUG DEVICE_NAME ": adc_start\n");

	data = ioread32(ADC_CTRL);
	data |= ADC_CTRL_AD_START_MASK;
	iowrite32(data, ADC_CTRL);
}

static irqreturn_t adc_interrupt (int irq, void * dev_id)
{
    adc_values[adc_channel] = ioread32(ADC_VALUE) & ADC_VALUE_MASK;
    printk(KERN_DEBUG DEVICE_NAME ": adc_interrupt(%d)=%d\n", adc_channel, adc_values[adc_channel]);

    if(current_task != NULL)
    {
        wake_up_process(current_task);
        current_task = NULL;
    }
	else
	{
		up(&channel_conversion);
	}

    return (IRQ_HANDLED);
}

static irqreturn_t gp_interrupt(int irq, void * dev_id)
{
    printk(KERN_INFO DEVICE_NAME ": gp_interrupt\n");

	if (down_trylock(&channel_conversion) == 0)
	{
		adc_start(0);
	}

    return (IRQ_HANDLED);
}

static void adc_exit (void)
{
    printk(KERN_DEBUG DEVICE_NAME ": adc_exit\n");
    free_irq (IRQ_LPC32XX_TS_IRQ, NULL);
    free_irq (IRQ_LPC32XX_GPI_01, NULL);
}


static ssize_t dev_read (struct file * file, char __user * buffer, size_t len, loff_t * offset)
{
    int current_offset;
	int dataLength;
	int bytesLeft;
	int written;
	int value;

	struct MessageData* data = file->private_data;

    if (*offset == 0)
    {
        printk (KERN_DEBUG DEVICE_NAME ": device_read(%d)\n", data->channel);

        if (data->channel < 0 || data->channel >= ADC_NUMCHANNELS)
        {
            return -EFAULT;
        }

		down(&channel_conversion);

        current_task = current;
        set_current_state(TASK_INTERRUPTIBLE);
        adc_start (data->channel);
        schedule();
		value = adc_values[data->channel];

		up(&channel_conversion);

        data->length = snprintf(data->buffer, MAX_BUFFER, "%d", value) + NULL_BYTE_ENDING;
    }

    current_offset = *offset;
	dataLength = data->length- current_offset;

	bytesLeft = copy_to_user(buffer, data->buffer + current_offset, dataLength);
	written = dataLength - bytesLeft;
	*offset = current_offset + written;

	return written;
}

static int dev_open (struct inode * inode, struct file * file)
{
    struct MessageData* data;
    int channel = iminor(file->f_dentry->d_inode);

    file->private_data = kmalloc(sizeof(struct MessageData), GFP_KERNEL);

    if (file == NULL)
    {
        return -ENOMEM;
    }

    data = (struct MessageData*)file->private_data;
    data->channel = channel;

    try_module_get(THIS_MODULE);

	return SUCCESS;
}

static int dev_release (struct inode* inode, struct file* fileToClose)
{
    if (fileToClose->private_data != NULL)
    {
        kfree(fileToClose->private_data);
        fileToClose->private_data = NULL;
    }

    printk (KERN_DEBUG DEVICE_NAME ": device_release()\n");
    module_put(THIS_MODULE);

	return SUCCESS;
}

int init_adc_module (void)
{
    int i;
	int error = alloc_chrdev_region(&deviceP, 0, ADC_NUMCHANNELS, DEVICE_NAME);

	if(error < 0)
	{
		printk(KERN_DEBUG DEVICE_NAME ": dynamic allocation of major number failed, error=%d\n", error);
		return error;
	}

	printk(KERN_DEBUG DEVICE_NAME ": major number=%d\n", MAJOR(deviceP));

	cdev_init(&cDevices, &fops);
	cDevices.owner = THIS_MODULE;
	cDevices.ops = &fops;

	error = cdev_add(&cDevices, deviceP, ADC_NUMCHANNELS);
	if(error < 0)
	{
		printk(KERN_WARNING DEVICE_NAME ": unable to add device, error=%d\n", error);
		return error;
	}

    for(i = 0; i < ADC_NUMCHANNELS; ++i)
    {
        printk(KERN_INFO DEVICE_NAME ": mknod /dev/adc%d c %d %d\n", i, MAJOR(deviceP), i);
    }
  
	adc_init();
	sema_init(&channel_conversion, 1);
	return SUCCESS;
}

void cleanup_adc_module()
{
	cdev_del(&cDevices);
	unregister_chrdev_region(deviceP, ADC_NUMCHANNELS);

	adc_exit();
}

module_init(init_adc_module);
module_exit(cleanup_adc_module);
