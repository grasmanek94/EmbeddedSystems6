#ifndef PWM_COMMON_H_INCLUDED
#define PWM_COMMON_H_INCLUDED

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
#include <mach/hardware.h>

MODULE_LICENSE("Custom");
MODULE_AUTHOR("Elviro & Rafal");
MODULE_DESCRIPTION("That's a kernel module wich will allow control of the PWM on LPC3250 boards");

#define MAX_BUFFER_SIZE (256)
#define DONE 0
#define ERROR -1
#define DEVICE_NAME "PWM"
#define bool char
#define true (1)
#define false (0)
#define MIN_FREQ_REGISTER (1)
#define MAX_FREQ_REGISTER (16)
#define MIN_RELOADV_REGISTER (0)
#define MAX_RELOADV_REGISTER (256)
#define FREQ_LOW (128)
#define FREQ_HIGH (50781) //.25

#define HERTZ_TO_MILLIHERTZ (1000)

enum PWMS
{
	PWM1,
	PWM2,
	MAX_PWMS
};

enum MINOR_FUNCTION
{
	MINOR_PWM1_ENABLE,
	MINOR_PWM1_FREQUENCY,
	MINOR_PWM1_DUTY,
	MINOR_PWM2_ENABLE,
	MINOR_PWM2_FREQUENCY,
	MINOR_PWM2_DUTY,
	MINOR_MAX_DEVICES
};


enum CLOCK_SOURCES
{
	CLOCK_SOURCE_RTC,
	CLOCK_SOURCE_PERIPH
};

struct PWM_ControlStructure
{
	uint32_t
		PWM_Duty : 8,
		PWM_ReloadV : 8,
		: 14,
		PWM_PinLevel : 1,
		PWM_Enabled : 1;
};

struct PWM_ClockStructure
{
	uint32_t
		: 1,
		PWM1_CLOCKSRC : 1,
		: 1,
		PWM2_CLOCKSRC : 1,
		PWM1_FREQ : 4,
		PWM2_FREQ : 4,
		: 20;
};

union PWM_Control
{
	struct PWM_ControlStructure control;
	uint32_t value;
};

union PWM_Clock
{
	struct PWM_ClockStructure clock;
	uint32_t value;
};

struct ClockCalculation
{
	enum CLOCK_SOURCES source;
	uint8_t reloadv; // 1 - 255
	uint8_t freq; // 1 - 15
	int64_t distance;
	int64_t max;
};

extern char	   kernel_buffer[MAX_BUFFER_SIZE];
extern size_t  kernel_buffer_length;

void set_pwm_frequency(int pwm);
void set_pwm_duty(int pwm);
void set_pwm_enabled(int pwm);
void get_pwm_frequency(int pwm);
void get_pwm_duty(int pwm);
void get_pwm_enabled(int pwm);

#endif