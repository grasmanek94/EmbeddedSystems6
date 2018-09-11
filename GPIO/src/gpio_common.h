#ifndef __GPIO_COMMON_H_INCLUDED
#define __GPIO_COMMON_H_INCLUDED

#include <linux/kernel.h>	  /* we're working with kernel*/
#include <linux/module.h>	  /*we're a bulding a module*/
#include <linux/kobject.h>	/*Necessary because we use sysfs*/
#include <linux/device.h>
#include <linux/fs.h>       /*for fops*/
#include <linux/init.h>
#include <linux/uaccess.h>  /* Required for the copy to user function*/
#include <linux/cdev.h>      /*for device registration*/
#include <linux/types.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <asm/errno.h>
#include <mach/hardware.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Elviro & Rafal");
MODULE_DESCRIPTION("That's a kernel module wich will allow control of the GPIO on LPC3250 boards");

#define MAX_BUFFER_SIZE (256)
#define DONE 0
#define ERROR -1
#define DEVICE_NAME "es6_gpio"
#define bool char
#define true (1)
#define false (0)
#define MAX_PORTS (4)
#define BITS_IN_REGISTER (32)

enum GPIO
{
	J1,
	J2,
	J3,
	GPIO_MAX
};

enum FUNCTION
{
	FUNCTION_DIRECTION,
	FUNCTION_VALUE,
	FUNCTION_MAX
};

struct MinorMapping
{
	bool mapped;
	enum GPIO port;
	int pin;
	enum FUNCTION function;
};

struct FunctionInfo
{
	enum FUNCTION function;
	char name[MAX_BUFFER_SIZE];
};

enum Action
{
	ACTION_DIRECTION,
	ACTION_STATE
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

struct MessageData
{
	char buffer[MAX_BUFFER_SIZE];
	size_t length;
};

extern struct PortAddresses port_info[MAX_PORTS];

#define AVAILABLE_PINS (27)
#define MAX_DEVICES (AVAILABLE_PINS * FUNCTION_MAX)

bool get_port_mapping(enum GPIO port, int pin, struct PortAddresses* address, int* mask);
enum Direction get_port_direction(enum GPIO port, int pin);
void set_port_direction(enum GPIO port, int pin, enum Direction direction);
enum State get_port_state(enum GPIO port, int pin);
void set_port_state(enum GPIO port, int pin, enum State state);
void configure_gpio(bool enable_gpio);

#endif
