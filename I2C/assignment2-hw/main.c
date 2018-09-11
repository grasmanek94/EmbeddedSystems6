#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <memory.h>
#include <string.h>
#include <errno.h>

#include "lmsensors_i2c-dev.h"

#define ArgsRead (7)
#define DEVICE "/dev/i2c-"

#define TYPE_MAX_STRING_SIZE(type) (1 + sizeof(type) * 2 + sizeof(type) / 2)
#define INT_MAX_STRING_SIZE TYPE_MAX_STRING_SIZE(int)
#define DEVICE_BASE_SIZE (10) // "/dev/i2c-"

#define OP_READ 'r'
#define OP_WRITE 'w'
#define ARG_BUS (1)
#define ARG_OPERATION (2)
#define ARG_SLAVEADDR (3)
#define ARG_REGISTER (4)
#define ARG_READLEN (5)
#define MIN_ARGS (6)
#define MAX_BYTES (8192)

#define bool char
#define true (1)
#define false (0)

#define DEBUG (1)

#ifdef DEBUG == 1
#define print_debug(...) printf(__VA_ARGS__)
#else
#define print_debug(...)
#endif

int deviceHandler = -1;

struct Parameters {
	char device[DEVICE_BASE_SIZE + INT_MAX_STRING_SIZE];
	char command;
	int address;
	char register_start;
	char length;
	char bytes[MAX_BYTES];
};

static int openDevice(char* device)
{
	if (device == NULL)
	{
		return -1;
	}

	int dev = open(device, O_RDWR);

	if (dev < 0)
	{
		return -1;
	}

	return dev;
}

static int readI2CBus(int device, char* buffer, int length)
{
	print_debug("%s:%d:%s <%d, %p, %d>\n", __FILE__, __LINE__, __func__, device, buffer, length);
	int readStatus = read(device, buffer, length);

	if (readStatus != length)
	{
		int errnum = errno;
		print_debug("%s:%d:%s ERROR=%d|%s\n", __FILE__, __LINE__, __func__, errnum, strerror(errnum));
		return -1;
	}
	return readStatus;
}

static int writeI2CBus(int device, char* buffer, int length)
{
	print_debug("%s:%d:%s <%d, %p, %d>\n", __FILE__, __LINE__, __func__, device, buffer, length);
	int writeStatus = write(device, buffer, length);
	if (writeStatus != length)
	{
		int errnum = errno;
		print_debug("%s:%d:%s ERROR=%d|%s\n", __FILE__, __LINE__, __func__, errnum, strerror(errnum));
		return -1;
	}
	return writeStatus;
}

static int selectRegisterI2CBus(int device, uint16_t regAddress)
{
	print_debug("%s:%d:%s <%d, %d>\n", __FILE__, __LINE__, __func__, device, regAddress);
	char registerBuffer[2];

	registerBuffer[0] = (regAddress >> 0) & 0xFF;
	registerBuffer[1] = (regAddress >> 8) & 0xFF;

	return writeI2CBus(device, registerBuffer, sizeof(registerBuffer) / sizeof(unsigned char));
}

static int readI2CRegister(int device, uint16_t regAddress, char * buffer, int length)
{
	print_debug("%s:%d:%s <%d, %d, %p, %d>\n", __FILE__, __LINE__, __func__, device, regAddress, buffer, length);
	if (selectRegisterI2CBus(device, regAddress) < 0)
	{
		int errnum = errno;
		print_debug("%s:%d:%s ERROR=%d|%s\n", __FILE__, __LINE__, __func__, errnum, strerror(errnum));
		return -1;
	}

	return readI2CBus(device, buffer, length);
}

static int writeI2CRegister(int device, uint16_t regAddress, char * buffer, int length)
{
	print_debug("%s:%d:%s <%d, %d, %p, %d>\n", __FILE__, __LINE__, __func__, device, regAddress, buffer, length);
	if (selectRegisterI2CBus(device, regAddress) < 0)
	{
		int errnum = errno;
		print_debug("%s:%d:%s ERROR=%d|%s\n", __FILE__, __LINE__, __func__, errnum, strerror(errnum));
		return -1;
	}

	return writeI2CBus(device, buffer, length);
}

static void printBuffer(char * buffer, int length)
{
	print_debug("%s:%d:%s <%p, %d>\n", __FILE__, __LINE__, __func__, buffer, length);
	char* strBuffer = malloc(length * 2);
	char * pstrBuffer = strBuffer;
	int holder;

	for (int i = 0; i < length; i++)
	{
		holder = sprintf(pstrBuffer, "%02x ", buffer[i]);
		pstrBuffer += holder;
	}

	printf("%s\n", strBuffer);
	free(strBuffer);
}

bool parseArguments(int length, char ** argv, struct Parameters * params)
{
	if (length < MIN_ARGS)
	{
		return false;
	}

	// i2c_hw 0 r slave register length
	// i2c_hw 0 w slave register byte1 ...
	params->command = argv[ARG_OPERATION][0];

	snprintf(params->device, (DEVICE_BASE_SIZE + INT_MAX_STRING_SIZE), "%s%d", DEVICE, atoi(argv[ARG_BUS]));
	sscanf(argv[ARG_SLAVEADDR], "%x", &params->address);

	params->register_start = atoi(argv[ARG_REGISTER]);

	if (params->address > 0xFFFF || params->address < 0)
	{
		return false;
	}

	if (params->command == OP_READ)
	{
		params->length = atoi(argv[ARG_READLEN]); // todo error handling
		if (params->length > MAX_BYTES)
		{
			return false;
		}
	}
	else if (params->command == OP_WRITE)
	{
		const int bytes_begin = (MIN_ARGS - 1);
		params->length = length - bytes_begin;

		if (params->length > MAX_BYTES)
		{
			return false;
		}

		for (int i = 0; i < params->length; ++i)
		{
			int val;
			if (sscanf(argv[bytes_begin + i], "%x", &val) != 1 || val < 0 || val > 0xFF)
			{
				return false;
			}

			params->bytes[i] = (char)val;
		}
	}
	else
	{
		return false;
	}

	return true;
}

void printUsage()
{
	#define MESSAGE_SIZE (2048)
	char message[MESSAGE_SIZE];


	snprintf(message, MESSAGE_SIZE, "\
Error: Invalid arguments\n\
Usage: ./i2c_hw <bus number> <operation r/w> <slave address> <register> <length to read / bytes to write... (max %d bytes)>\n\
Example r: ./i2c_hw 0 r c0 16 04 (reads 4 bytes [max=%d bytes], from regs 0x16, 0x17, 0x18 and 0x19)\n\
Example w: ./i2c_hw 0 w c0 16 00 10 88 (writes bytes 0x00 0x10 0x88 to register 0x16)", MAX_BYTES, MAX_BYTES);
	
	#undef MESSAGE_SIZE
	perror(message);
}

bool prepareDevice(int * handler, struct Parameters* params)
{
	print_debug("%s:%d:%s <%p, %p>\n", __FILE__, __LINE__, __func__, handler, params);
	int selectStatus = -1;
	*handler = openDevice(params->device);

	if (*handler < 0)
	{
		int errnum = errno;
		print_debug("%s:%d:%s ERROR(if (*handler < 0))=%d|%s\n", __FILE__, __LINE__, __func__, errnum, strerror(errnum));
		return false;
	}

	selectStatus = ioctl(*handler, I2C_SLAVE, (__u16)params->address);

	if (selectStatus < 0)
	{
		int errnum = errno;
		print_debug("%s:%d:%s ERROR(if (selectStatus < 0))=%d|%s\n", __FILE__, __LINE__, __func__, errnum, strerror(errnum));
		close(*handler);
		return false;
	}

	return true;
}

int processOperation(int devHandler, struct Parameters* params)
{
	print_debug("%s:%d:%s <%d, %p>\n", __FILE__, __LINE__, __func__, devHandler, params);
	if (params->command == OP_READ)
	{
		return readI2CRegister(devHandler, params->address, params->bytes, params->length);
	}
	else if (params->command == OP_WRITE)
	{
		return writeI2CRegister(devHandler, params->address, params->bytes, params->length);
	}
	else
	{
		print_debug("%s:%d:%s ERROR=NOTHING TO DO\n", __FILE__, __LINE__, __func__);
		// nothing to do here
		return -1;
	}
}

int main(int argc, char* argv[])
{

	struct Parameters params;
	memset(&params, 0, sizeof(params));

	if (!parseArguments(argc, argv, &params))
	{
		printUsage();
		return 1;
	}

	printf("\
		Parameters:\n\
		Device: %s\n\
		Slave address: 0x%x\n\
		Command: %c\n\
		Register: 0x%x\n\
		Length: %d\n",
		params.device, 
		params.address, 
		params.command,
		params.register_start,
		params.length
	);

	if (!prepareDevice(&deviceHandler, &params))
	{
		perror("Error while openning device");
		return 1;
	}

	if (processOperation(deviceHandler, &params) < 0)
	{
		perror("Error while processing operation");
		close(deviceHandler);
		return 1;
	}

	if (params.command == OP_READ)
	{
		printBuffer(params.bytes, params.length);
	}

	close(deviceHandler);
	return 0;
}
