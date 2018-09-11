#include "pwm_common.h"

bool    deviceOpened = false;

dev_t          deviceP;
struct cdev    cDevices;

static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
char	   kernel_buffer[MAX_BUFFER_SIZE];
size_t  kernel_buffer_length;

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
  if(deviceNode == NULL && fileToOpen == NULL)
  {
    return ERROR;
  }
  else if(deviceOpened)
  {
    return -EBUSY;
  }
  else
  {
    deviceOpened = true;
    try_module_get(THIS_MODULE);
    return DONE;
  }
}

static int dev_release(struct inode * deviceNode, struct file * fileToClose)
{
	if (deviceOpened)
	{
		deviceOpened = false;
		module_put(THIS_MODULE);
	}

    return DONE;
}

static void select_device(int minor, bool get)
{
	#define PROCESS_SELECT_OP(should_get,getter_function,setter_function,pwm) \
	{ \
		if(should_get) { \
			getter_function(pwm); \
		} else { \
			setter_function(pwm); \
	}}

	switch (minor)
	{
	case MINOR_PWM1_ENABLE:
		PROCESS_SELECT_OP(get, get_pwm_enabled, set_pwm_enabled, PWM1);
		break;
	case MINOR_PWM1_FREQUENCY:
		PROCESS_SELECT_OP(get, get_pwm_frequency, set_pwm_frequency, PWM1);
		break;
	case MINOR_PWM1_DUTY:
		PROCESS_SELECT_OP(get, get_pwm_duty, set_pwm_duty, PWM1);
		break;
	case MINOR_PWM2_ENABLE:
		PROCESS_SELECT_OP(get, get_pwm_enabled, set_pwm_enabled, PWM2);
		break;
	case MINOR_PWM2_FREQUENCY:
		PROCESS_SELECT_OP(get, get_pwm_frequency, set_pwm_frequency, PWM2);
		break;
	case MINOR_PWM2_DUTY:
		PROCESS_SELECT_OP(get, get_pwm_duty, set_pwm_duty, PWM2);
		break;
	default:
		break;
	}
	#undef PROCESS_SELECT_OP
}

static void process_read_kernel_buffer(int minor)
{
	kernel_buffer[kernel_buffer_length] = 0;
	printk(KERN_DEBUG "process_read_kernel_buffer(%d): %s\n", minor, kernel_buffer);
	select_device(minor, true);
}

static void process_write_kernel_buffer(int minor)
{
	kernel_buffer[kernel_buffer_length] = 0;
	printk(KERN_DEBUG "process_write_kernel_buffer(%d): %s\n", minor, kernel_buffer);
	select_device(minor, false);
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
	int current_offset;
	int dataLength;
	int bytesLeft;
	int written;

	if (*offset == 0)
	{
		process_read_kernel_buffer(iminor(filep->f_dentry->d_inode));
	}

	current_offset = *offset;
	dataLength = kernel_buffer_length - current_offset;

	bytesLeft = copy_to_user(buffer, kernel_buffer + current_offset, dataLength);
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

	if (len > sizeof(kernel_buffer))
	{
		return ERROR; // sorry, not gonna fit so bugger off
	}

	current_offset = *offset;
	dataLength = len - current_offset;
	bytesLeft = copy_from_user(kernel_buffer + current_offset, buffer, dataLength);
	written = dataLength - bytesLeft;
	*offset = current_offset + written;

	if (*offset >= len)
	{
		kernel_buffer_length = len;
		process_write_kernel_buffer(iminor(filep->f_dentry->d_inode));
	}

	return written;
}

static void pwm_exit(void)
{
	cdev_del(&cDevices);
	unregister_chrdev_region(deviceP, MINOR_MAX_DEVICES);
}

static int pwm_init(void)
{
  int status;
  status = alloc_chrdev_region(&deviceP, 0, MINOR_MAX_DEVICES, DEVICE_NAME);

  if(status != 0)
  {
    printk(KERN_ALERT "alloc_chrdev_region faild\n");
    return status;
  }
  else
  {
    cdev_init(&cDevices, &fops);
    status = cdev_add(&cDevices, deviceP, MINOR_MAX_DEVICES);

    if(status != 0)
    {
      printk(KERN_ALERT "cdev_add faild\n");
      pwm_exit();
      return status;
    }
  }

  printk(KERN_ALERT "mknod /dev/PWM1_enable c %d %d\n", MAJOR(deviceP), MINOR_PWM1_ENABLE);
  printk(KERN_ALERT "mknod /dev/PWM1_frequency c %d %d\n", MAJOR(deviceP), MINOR_PWM1_FREQUENCY);
  printk(KERN_ALERT "mknod /dev/PWM1_duty c %d %d\n", MAJOR(deviceP), MINOR_PWM1_DUTY);
  printk(KERN_ALERT "mknod /dev/PWM2_enable c %d %d\n", MAJOR(deviceP), MINOR_PWM2_ENABLE);
  printk(KERN_ALERT "mknod /dev/PWM2_frequency c %d %d\n", MAJOR(deviceP), MINOR_PWM2_FREQUENCY);
  printk(KERN_ALERT "mknod /dev/PWM2_duty c %d %d\n", MAJOR(deviceP), MINOR_PWM2_DUTY);

  return DONE;
}

module_init(pwm_init);
module_exit(pwm_exit);