#include "gpio_common.h"

dev_t          deviceP;
struct cdev    cDevices;

static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
struct class*  gpio_class;
static int total_allocated_devices = 0;

struct MinorMapping mappings[MAX_DEVICES];

char connector_info[GPIO_MAX][MAX_BUFFER_SIZE] =
{
	"J1",
	"J2",
	"J3"
};

struct FunctionInfo function_info[FUNCTION_MAX] = 
{
	{.name = "direction",.function = FUNCTION_DIRECTION },
	{.name = "value",.function = FUNCTION_VALUE }
};

static struct file_operations fops =
{
   .owner = THIS_MODULE,
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};

static int dev_open(struct inode * deviceNode, struct file * fileToOpen)
{
	try_module_get(THIS_MODULE);

	fileToOpen->private_data = kmalloc(sizeof(struct MessageData), GFP_KERNEL);

	if (fileToOpen == NULL)
	{
		return -ENOMEM;
	}

	return DONE;
}

static int dev_release(struct inode * deviceNode, struct file * fileToClose)
{
	if (fileToClose->private_data != NULL)
	{
		kfree(fileToClose->private_data);
	}

	module_put(THIS_MODULE);

	return DONE;
}

static void perform_device_operation(struct MessageData* data, int minor, bool get)
{
	bool kernel_buffer_value;
	struct MinorMapping* entry;
	
	printk(KERN_DEBUG "perform_device_operation\n");

	entry = &mappings[minor];

	if (!entry->mapped)
	{
		printk(KERN_DEBUG "Not Mapped!\n");
		return;
	}
	else
	{
		printk(KERN_DEBUG "IS Mapped!\n");
	}

	kernel_buffer_value = data->buffer[0] != '0';

	#define PROCESS_SELECT_OP(should_get,getter_function,setter_function,port,pin,value) \
	{ \
		if(should_get) { \
			value = getter_function(port,pin); \
		} else { \
			setter_function(port,pin,value); \
	}	}

	switch (entry->function)
	{
	case FUNCTION_DIRECTION:
		printk(KERN_DEBUG "FUNCTION_DIRECTION\n");
		PROCESS_SELECT_OP(
			get, 
			get_port_direction, 
			set_port_direction,
			entry->port, 
			entry->pin, 
			kernel_buffer_value
		);
		break;
	case FUNCTION_VALUE:
		printk(KERN_DEBUG "FUNCTION_VALUE\n");
		PROCESS_SELECT_OP(
			get, 
			get_port_state, 
			set_port_state, 
			entry->port, 
			entry->pin, 
			kernel_buffer_value
		);
		break;
	default:
		printk(KERN_DEBUG "Unknown function: %d!\n", entry->function);
		// nothing to do
		return;
	}

	if (get)
	{
		printk(KERN_DEBUG "Get\n");
		data->buffer[0] = (kernel_buffer_value ? '1' : '0');
		data->length = 1;
		data->buffer[data->length] = 0;
	}

	#undef PROCESS_SELECT_OP
}

static void process_kernel_buffer(struct MessageData* data, int minor, bool read)
{
	data->buffer[data->length] = 0;
	printk(KERN_DEBUG "1) process_%s_kernel_buffer(%d): (%d) %s\n", (read ? "read" : "write"), minor, data->length, data->buffer);
	if (data->length > 0 || read)
	{
		perform_device_operation(data, minor, read);
	}
	printk(KERN_DEBUG "2) process_%s_kernel_buffer(%d): (%d) %s\n", (read ? "read" : "write"), minor, data->length, data->buffer);
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
	int current_offset;
	int dataLength;
	int bytesLeft;
	int written;
	struct MessageData* data = filep->private_data;
	if (*offset == 0)
	{
		process_kernel_buffer(data, iminor(filep->f_dentry->d_inode), true);
	}

	current_offset = *offset;
	dataLength = data->length- current_offset;

	bytesLeft = copy_to_user(buffer, data->buffer + current_offset, dataLength);
	written = dataLength - bytesLeft;
	*offset = current_offset + written;

	return written;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
	int current_offset;
	int dataLength;
	int bytesLeft;
	int written;
	struct MessageData* data = filep->private_data;

	if (len > sizeof(data->buffer))
	{
		return ERROR;
	}

	current_offset = *offset;
	dataLength = len - current_offset;
	bytesLeft = copy_from_user(data->buffer + current_offset, buffer, dataLength);
	written = dataLength - bytesLeft;
	*offset = current_offset + written;

	if (*offset >= len)
	{
		data->length = len;
		process_kernel_buffer(data, iminor(filep->f_dentry->d_inode), false);
	}

	return written;
}

static void gpio_exit(void)
{
	int dev;

	for (dev = 0; dev < total_allocated_devices; ++dev)
	{
		device_destroy(gpio_class, MKDEV(MAJOR(deviceP), dev));
	}

	cdev_del(&cDevices);
	class_destroy(gpio_class);
	unregister_chrdev_region(deviceP, MAX_DEVICES);
}

static int gpio_init(void)
{
	int status;
	enum GPIO dev;
	int pin;
	int mapping;
	int port;
	enum FUNCTION function;
	struct device* dc;

	status = alloc_chrdev_region(&deviceP, 0, MAX_DEVICES, DEVICE_NAME);

	configure_gpio(true);
	gpio_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(gpio_class))
	{
		gpio_exit();
		return PTR_ERR(gpio_class);
	}

	total_allocated_devices = 0;

	for (port = 0; port < MAX_PORTS; ++port)
	{
		for (mapping = 0; mapping < port_info[port].MAPPING_PINS; ++mapping)
		{
			dev = port_info[port].MAPPING[mapping].port;
			pin = port_info[port].MAPPING[mapping].pin;

			if (get_port_mapping(dev, pin, NULL, NULL))
			{
				for (function = 0; function < FUNCTION_MAX; ++function)
				{
					dc = device_create(gpio_class, NULL, MKDEV(MAJOR(deviceP), total_allocated_devices), NULL, "%s/%d/%s", connector_info[dev], pin, function_info[function].name);
					if (IS_ERR(dc))
					{
						total_allocated_devices++;
						gpio_exit();
						return ERROR;
					}
					
					mappings[total_allocated_devices].mapped = true;
					mappings[total_allocated_devices].port = dev;
					mappings[total_allocated_devices].pin = pin;
					mappings[total_allocated_devices].function = function_info[function].function;

					printk(KERN_DEBUG "mknod /dev/%s.%s.%d-%s c %d %d\n", DEVICE_NAME, connector_info[dev], pin, function_info[function].name, MAJOR(deviceP), total_allocated_devices);
					total_allocated_devices++;
				}
			}
		}
	}

	if (status != 0)
	{
		printk(KERN_ALERT "alloc_chrdev_region faild\n");
		return status;
	}
	else
	{
		cdev_init(&cDevices, &fops);
		status = cdev_add(&cDevices, deviceP, MAX_DEVICES);

		if (status != 0)
		{
			printk(KERN_ALERT "cdev_add faild\n");
			gpio_exit();
			return status;
		}
	}

	return DONE;
}

module_init(gpio_init);
module_exit(gpio_exit);
