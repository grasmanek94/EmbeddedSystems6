#include <linux/kernel.h>	/* we're working with kernel*/
#include <linux/module.h>	/*we're a bulding a module*/
#include <linux/kobject.h>	/*Necessary because we use sysfs*/
#include <linux/device.h>
#include <linux/io.h> /* remap */
#include <linux/ioport.h> /* request memory */
#include <linux/sched.h>        /* For current */
#include <linux/tty.h>          /* For the tty declarations */
#include <linux/version.h>      /* For LINUX_VERSION_CODE */

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Elviro & Rafał");
MODULE_DESCRIPTION("sysfs es6 hw register");

#define sysfs_dir  "es6"
#define sysfs_file "hw"

#define sysfs_max_data_size 1024 /* due to limitations of sysfs, you mustn't go above PAGE_SIZE, 1k is already a *lot* of information for sysfs! */
static char sysfs_buffer[sysfs_max_data_size + 1]; /* an extra byte for the '\0' terminator */
static ssize_t used_buffer_size = 0;

static void print_string(bool kern, const char *format, ...)
{
	struct tty_struct *my_tty;
	char str[sysfs_max_data_size - 8]; // -8 because we need to limit the framesize to 1024 total

	va_list arglist;
	va_start(arglist, format);
	vsnprintf(str, sizeof(str), format, arglist);
	va_end(arglist);

	if (kern)
	{
		printk(str);
	}
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,5) )
	my_tty = current->tty;
#else
	my_tty = current->signal->tty;
#endif

	if (my_tty != NULL) {
		((my_tty->driver->ops)->write) (my_tty,
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9) )             
			0,               
#endif
			str,             
			strlen(str));   

#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9) )             
		((my_tty->driver->ops)->write) (my_tty, 0, "\015\012", 2);
#else
		((my_tty->driver->ops)->write) (my_tty, "\015\012", 2);
#endif
	}
}

static void write_register(unsigned int address, int value)
{
	volatile int* remap = (int*)ioremap(address, 4);
	int result;

	if (remap == NULL)
	{
		print_string(true, KERN_DEBUG "write_register 0x%x %x error (remap == null)\n", address, value);
		return;
	}
	
	result = iowrite32(value, remap);
	print_string(true, KERN_DEBUG "write_register 0x%x(%p) %x result %x\n", address, remap, value, result);

	iounmap((void*)remap);
}

static int read_register(unsigned int address)
{
	volatile int* remap = (int*)ioremap(address, 4);
	int value;

	if (remap == NULL)
	{
		print_string(true, KERN_DEBUG "read_register 0x%x error (remap == null)\n", address);
		return 0;
	}

	value = ioread32(remap);
	print_string(true, KERN_DEBUG "read_register 0x%x(%p) result %x\n", address, remap, value);

	iounmap((void*)remap);

	return value;
}

static ssize_t
sysfs_show(struct device *dev,
	struct device_attribute *attr,
	char *buffer)
{
	print_string(true, KERN_DEBUG "sysfile_read (/sys/kernel/%s/%s) called\n", sysfs_dir, sysfs_file);

	return sprintf(buffer, "%s", sysfs_buffer);
}

static void process_buffer(void)
{
	char mode;
	unsigned int address;
	int value;
	int chars;
	int max_registers;
	unsigned int current_address;
	unsigned int end_address;
	int length;

	if (used_buffer_size < 1)
	{
		return;
	}

	if (sscanf(sysfs_buffer, "%c %x %x", &mode, &address, &value) != 3)
	{
		sprintf(sysfs_buffer, "ERROR -1: Incorrect parameter format, expected <r/w> <[address]> <[amount/value]>");
		return;
	}

	if(mode != 'r' && mode != 'w')
	{
		sprintf(sysfs_buffer, "ERROR -1: Incorrect parameter format, expected <r/w> <[address]> <[amount/value]>");
		return;
	}

	if (mode == 'w')
	{
		write_register(address, value);
		return;
	}

	chars = strlen("00000000 ");
	max_registers = sysfs_max_data_size / chars;

	if (value > max_registers || value < 1)
	{
		sprintf(sysfs_buffer, "ERROR -2: Buffer overflow prevented, cannot read more than %d registers at once", max_registers);
		return;
	}

	current_address = address;
	end_address = address + value * 4;

	sysfs_buffer[0] = '\0';
	length = 0;
	while (current_address < end_address)
	{
		length += sprintf(sysfs_buffer + length, "%x ", (unsigned int)read_register(current_address));
		current_address += 4;
	}

	print_string(false, sysfs_buffer);
}

static ssize_t
sysfs_store(struct device *dev,
	struct device_attribute *attr,
	const char *buffer,
	size_t count)
{
	used_buffer_size = count > sysfs_max_data_size ? sysfs_max_data_size : count;

	print_string(true, KERN_DEBUG "sysfile_write (/sys/kernel/%s/%s) called, buffer: %s, count: %d\n", sysfs_dir, sysfs_file, buffer, count);

	memcpy(sysfs_buffer, buffer, used_buffer_size);
	sysfs_buffer[used_buffer_size] = '\0';

	process_buffer();

	return used_buffer_size;
}

static DEVICE_ATTR(hw, S_IWUGO | S_IRUGO, sysfs_show, sysfs_store);

static struct attribute *attrs[] = {
	&dev_attr_hw.attr,
	NULL   /* need to NULL terminate the list of attributes */
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};
static struct kobject *sysfs_obj = NULL;


int __init sysfs_init(void)
{
	int result = 0;

	sysfs_obj = kobject_create_and_add(sysfs_dir, kernel_kobj);
	if (sysfs_obj == NULL)
	{
		print_string(true, KERN_INFO "%s module failed to load: kobject_create_and_add failed\n", sysfs_file);
		return -ENOMEM;
	}

	result = sysfs_create_group(sysfs_obj, &attr_group);
	if (result != 0)
	{
		/* creating files failed, thus we must remove the created directory! */
		print_string(true, KERN_INFO "%s module failed to load: sysfs_create_group failed with result %d\n", sysfs_file, result);
		kobject_put(sysfs_obj);
		return -ENOMEM;
	}

	print_string(true, KERN_INFO "/sys/kernel/%s/%s created\n", sysfs_dir, sysfs_file);
	return result;
}

void __exit sysfs_exit(void)
{
	kobject_put(sysfs_obj);
	print_string(true, KERN_INFO "/sys/kernel/%s/%s removed\n", sysfs_dir, sysfs_file);
}

module_init(sysfs_init);
module_exit(sysfs_exit);
