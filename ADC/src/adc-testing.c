#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ktime.h>
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

//////////// GPIO
enum GPIO
{
	J1,
	J2,
	J3,
	GPIO_MAX
};

enum Direction
{
	DIRECTION_INPUT,
	DIRECTION_OUTPUT
};

enum State
{
	STATE_LOW,
	STATE_HIGH
};

void set_port_direction(enum GPIO port, int pin, enum Direction direction);
void set_port_state(enum GPIO port, int pin, enum State state);
void configure_gpio(bool enable_gpio);
bool gpio_val;
/////////////////

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
static struct timespec interrupt_measure;
static ktime_t interrupt_measure_result[3];

static bool perform_measure = false;

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
        printk(KERN_ALERT DEVICE_NAME ":ADC IRQ request failed\n");
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
	if (perform_measure)
	{
		perform_measure = false;
		getnstimeofday(&interrupt_measure);
		interrupt_measure_result[1] = timespec_to_ktime(interrupt_measure);
		interrupt_measure_result[2] = ktime_sub(interrupt_measure_result[1], interrupt_measure_result[0]);
		interrupt_measure = ktime_to_timespec(interrupt_measure_result[2]);
		printk(KERN_DEBUG DEVICE_NAME ": adc_interrupt measured %ld seconds and %ld nanoseconds\n", (long int)interrupt_measure.tv_sec, interrupt_measure.tv_nsec);
	}

    adc_values[adc_channel] = ioread32(ADC_VALUE) & ADC_VALUE_MASK;
    printk(KERN_DEBUG DEVICE_NAME ": adc_interrupt(%d)=%d\n", adc_channel, adc_values[adc_channel]);

    if(current_task != NULL)
    {
        wake_up_process(current_task);
        current_task = NULL;
    }

    return (IRQ_HANDLED);
}

static irqreturn_t gp_interrupt(int irq, void * dev_id)
{
	gpio_val ^= 1;
	set_port_state(J3, 40, gpio_val);
	set_port_state(J3, 47, gpio_val);
	set_port_state(J3, 54, gpio_val);

    printk(KERN_INFO DEVICE_NAME ": gp_interrupt\n");

    adc_start (0);
	getnstimeofday(&interrupt_measure);
	interrupt_measure_result[0] = timespec_to_ktime(interrupt_measure);
	perform_measure = true;

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

	struct MessageData* data = file->private_data;

    if (*offset == 0)
    {
		// start sem here
		//

        printk (KERN_DEBUG DEVICE_NAME ": device_read(%d)\n", data->channel);

        if (data->channel < 0 || data->channel >= ADC_NUMCHANNELS)
        {
            return -EFAULT;
        }

        current_task = current;
        set_current_state(TASK_INTERRUPTIBLE);
        adc_start (data->channel);
        schedule();

		// release sem here
		//

        data->length = snprintf(data->buffer, MAX_BUFFER, "%d", adc_values[data->channel]) + 1;
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

	//// GPIO
	configure_gpio(true);
	set_port_direction(J3, 40, DIRECTION_OUTPUT);
	set_port_direction(J3, 47, DIRECTION_OUTPUT);
	set_port_direction(J3, 54, DIRECTION_OUTPUT);
	////

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

// gpio stuff to measure eint0
#define MAX_PORTS (4)
#define BITS_IN_REGISTER (32)

struct ConnectionMapping
{
	enum GPIO port;
	int pin;
};

struct PortAddresses
{
	uint32_t ENABLE_MASK;
	uint32_t* MUX_SET;
	uint32_t* MUX_CLR;
	uint32_t* MUX_STATE;
	uint32_t* INP_STATE;
	uint32_t* OUTP_SET;
	uint32_t* OUTP_CLR;
	uint32_t* OUTP_STATE;
	uint32_t* DIR_SET;
	uint32_t* DIR_CLR;
	uint32_t* DIR_STATE;
	int MAPPING_OFFSET;
	int MAPPING_PINS;
	struct ConnectionMapping MAPPING[BITS_IN_REGISTER];
};

bool get_port_mapping(enum GPIO port, int pin, struct PortAddresses* address, int* mask);
enum Direction get_port_direction(enum GPIO port, int pin);
enum State get_port_state(enum GPIO port, int pin);

struct PortAddresses port_info[MAX_PORTS] =
{
	{
		.ENABLE_MASK = 0xFF,

		// MUX_SET & MUX_CLR = Swapped for consistency in enabling/disabling GPIO, see docs.
		.MUX_SET = io_p2v(0x40028124),
		.MUX_CLR = io_p2v(0x40028120),
		.MUX_STATE = io_p2v(0x40028128),

		.INP_STATE = io_p2v(0x40028040),

		.OUTP_SET = io_p2v(0x40028044),
		.OUTP_CLR = io_p2v(0x40028048),
		.OUTP_STATE = io_p2v(0x4002804C),

		.DIR_SET = io_p2v(0x40028050),
		.DIR_CLR = io_p2v(0x40028054),
		.DIR_STATE = io_p2v(0x40028058),

		.MAPPING_OFFSET = 0,
		.MAPPING_PINS = 1,
		.MAPPING =
		{
			{ J3, 40 }
		}
	},

	{
		.ENABLE_MASK = 0xFFFFFF,

		.MUX_SET = io_p2v(0x40028130),
		.MUX_CLR = io_p2v(0x40028134),
		.MUX_STATE = io_p2v(0x40028138),

		.INP_STATE = io_p2v(0x40028060),

		.OUTP_SET = io_p2v(0x40028064),
		.OUTP_CLR = io_p2v(0x40028068),
		.OUTP_STATE = io_p2v(0x4002806C),

		.DIR_SET = io_p2v(0x40028070),
		.DIR_CLR = io_p2v(0x40028074),
		.DIR_STATE = io_p2v(0x40028078),

		.MAPPING_OFFSET = 0,
		.MAPPING_PINS = 0

		// Port 1 has no external mappings, this is not tested
	},

	{
		.ENABLE_MASK = 0x08,

		.MUX_SET = io_p2v(0x40028028),
		.MUX_CLR = io_p2v(0x4002802C),
		.MUX_STATE = io_p2v(0x40028030),

		.INP_STATE = io_p2v(0x4002801C),

		.OUTP_SET = io_p2v(0x40028020),
		.OUTP_CLR = io_p2v(0x40028024),
		.OUTP_STATE = io_p2v(0x40028028),

		.DIR_SET = io_p2v(0x40028010),
		.DIR_CLR = io_p2v(0x40028014),
		.DIR_STATE = io_p2v(0x40028018),

		.MAPPING_OFFSET = 0,
		.MAPPING_PINS = 1,
		.MAPPING =
		{
			{ J3, 47 }
		}
	},

	{
		.ENABLE_MASK = 0x33,

		// MUX_SET & MUX_CLR = Swapped for consistency in enabling/disabling GPIO, see docs.
		.MUX_SET = io_p2v(0x4002802C),
		.MUX_CLR = io_p2v(0x40028028),
		.MUX_STATE = io_p2v(0x40028030),

		.INP_STATE = io_p2v(0x40028000),

		.OUTP_SET = io_p2v(0x40028004),
		.OUTP_CLR = io_p2v(0x40028008),
		.OUTP_STATE = io_p2v(0x4002800C),

		.DIR_SET = io_p2v(0x40028010),
		.DIR_CLR = io_p2v(0x40028014),
		.DIR_STATE = io_p2v(0x40028018),

		.MAPPING_OFFSET = 25,
		.MAPPING_PINS = 1,
		.MAPPING =
		{
			{ J3, 54 }
		}
	}
};

bool get_port_mapping(enum GPIO port, int pin, struct PortAddresses* address, int* mask)
{
	int i;
	int j;
	struct ConnectionMapping* mapping;

	if (pin < 0 || port < 0)
	{
		return false;
	}

	for (i = 0; i < MAX_PORTS; ++i)
	{
		for (j = 0; j < port_info[i].MAPPING_PINS; ++j)
		{
			mapping = &port_info[i].MAPPING[j];
			if (mapping->port == port && mapping->pin == pin)
			{
				if (address != NULL)
				{
					*address = port_info[i];
				}
				if (mask != NULL)
				{
					*mask = 1 << (j + port_info[i].MAPPING_OFFSET);
				}
				return true;
			}
		}
	}

	return false;
}

enum Direction get_port_direction(enum GPIO port, int pin)
{
	struct PortAddresses address;
	int mask;

	if (!get_port_mapping(port, pin, &address, &mask))
	{
		return 0;
	}

	return
		((ioread32(address.DIR_STATE) & mask) == 0) ?
		DIRECTION_INPUT :
		DIRECTION_OUTPUT;
}

void set_port_direction(enum GPIO port, int pin, enum Direction direction)
{
	struct PortAddresses address;
	int mask;

	if (!get_port_mapping(port, pin, &address, &mask))
	{
		return;
	}

	iowrite32(
		mask,
		(direction == DIRECTION_INPUT) ?
		address.DIR_CLR :
		address.DIR_SET
	);
}

enum State get_port_state(enum GPIO port, int pin)
{
	struct PortAddresses address;
	int mask;

	if (!get_port_mapping(port, pin, &address, &mask))
	{
		return 0;
	}

	return
		(ioread32(
		(get_port_direction(port, pin) == DIRECTION_INPUT) ?
			address.INP_STATE :
			address.OUTP_STATE
		) & mask) != 0 ?
		STATE_HIGH :
		STATE_LOW;
}

void set_port_state(enum GPIO port, int pin, enum State state)
{
	struct PortAddresses address;
	int mask;

	if (!get_port_mapping(port, pin, &address, &mask))
	{
		return;
	}

	iowrite32(
		mask,
		state == STATE_LOW ?
		address.OUTP_CLR :
		address.OUTP_SET
	);
}

void configure_gpio(bool enable_gpio)
{
	int i;

	for (i = 0; i < MAX_PORTS; ++i)
	{
		if (port_info[i].MAPPING_PINS > 0)
		{
			iowrite32(port_info[i].ENABLE_MASK, enable_gpio ? port_info[i].MUX_SET : port_info[i].MUX_CLR);
		}
	}
}
