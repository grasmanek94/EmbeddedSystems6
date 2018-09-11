#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "lmsensors_i2c-dev.h"
#include "pca9532.h"

#define DEVICE "/dev/i2c-0"
#define MAX_LEDS (8)
#define MAX_LED_SELECTORS (2)
#define BASE_DECIMAL (10)
#define PROGRAM_REQUIRED_ARGC (3)
#define ARG_FREQ (1)
#define ARG_PWR (2)
#define ARG_LED_MIN (3)
#define ARG_LED_MAX (ARG_LED_MIN + MAX_LEDS)
#define MIN_POWER (0)
#define MAX_POWER (100)
#define MIN_FREQ (0.000f)
#define VALID_DEVICE_HANDLE(h) (!((h) < 0))
#define NANOSECOND (1000000000L)
#define CLOSE_INVALIDATE(h) (close(h), (h) = -1)

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 

int device_handle = -1;

#define bool char
#define true (1)
#define false (0)

struct Selector
{
	unsigned char reg;
	unsigned char val;
};

struct Parameters
{
	float frequency;
	struct timespec frequency_spec;
	int power;
	unsigned char duty_cycle;
	bool leds[MAX_LEDS];
	struct Selector selectors[MAX_LED_SELECTORS];
};

volatile sig_atomic_t stop;

void
inthand(int signum)
{
	stop = 1;
}

void cleanup()
{
	if (!VALID_DEVICE_HANDLE(device_handle))
	{
		CLOSE_INVALIDATE(device_handle);
	}
}

void print_usage()
{
	perror("\
Usage: ./led <hz> <power %> [<leds>...]\n\
Example: ./led 1.5 10 0 2 4 6\n\
To blink leds 0 2 4 and 6 at 1.5 Hz at 10% duty cycle");

}

bool parse_arguments(int argc, char** argv, struct Parameters* params)
{
	memset(params->leds, true, sizeof(params->leds));

	if (argc < PROGRAM_REQUIRED_ARGC)
	{
		return false;
	}

	// freq
	params->frequency = strtof(argv[ARG_FREQ], NULL);
	if (errno == ERANGE || params->frequency < MIN_FREQ) {
		return false;
	}

	if (params->frequency != 0)
	{
		float seconds = (1.0f / params->frequency);
		float seconds_part = seconds - (float)((long)seconds);
		seconds = (float)((long)seconds);

		params->frequency_spec.tv_nsec = seconds_part * NANOSECOND;
		params->frequency_spec.tv_sec = seconds;
	}
	else
	{
		params->frequency_spec.tv_nsec = 0;
		params->frequency_spec.tv_sec = 0;
	}

	printf("Frequency set to %s Hz\n", argv[ARG_FREQ]);
	printf("Sleep data: %d/%d\n", (int)params->frequency_spec.tv_sec, (int)params->frequency_spec.tv_nsec);

	// pwr
	params->power = strtol(argv[ARG_PWR], NULL, BASE_DECIMAL);
	if (errno == ERANGE || params->power < MIN_POWER || params->power > MAX_POWER) {
		return false;
	}

	params->duty_cycle = (unsigned char)(((float)params->power / 100.0f) * 256.0f);
	printf("Power set to 0x%X/0xFF (%d%%)\n", params->duty_cycle, params->power);

	// determine which leds to enable
	if (argc > PROGRAM_REQUIRED_ARGC)
	{
		printf("LEDs enabled:");
		memset(params->leds, false, sizeof(params->leds));
		for (int i = ARG_LED_MIN; i < argc && i < ARG_LED_MAX; ++i)
		{
			uintmax_t num = strtoumax(argv[i], NULL, BASE_DECIMAL);
			if ((num == UINTMAX_MAX && errno == ERANGE) || num >= MAX_LEDS)
			{
				return false;
			}

			params->leds[num] = true;
			printf(" %d", i);
		}
	}
	else
	{
		printf("All LEDs enabled");
	}
	printf("\n");

	int ledmode = PCA9532_LEDMODE_OFF;

	switch(params->power)
	{
	case MAX_POWER:
		ledmode = PCA9532_LEDMODE_ON;
		break;
	case MIN_POWER:
		ledmode = PCA9532_LEDMODE_OFF;
		break;
	default:
		ledmode = PCA9532_LEDMODE_PWM0;
		break;
	}

	// need to setup the registers
	params->selectors[0].reg = PCA9532_REGISTER_LS2;
	params->selectors[1].reg = PCA9532_REGISTER_LS3;

	for (int i = 0; i < MAX_LEDS; ++i)
	{
		int leds_per_selector = (MAX_LEDS / MAX_LED_SELECTORS);
		int selector = i / leds_per_selector;
		int led = i % leds_per_selector;
		unsigned char led_selector = PCA9532_SET_LED(led,
			params->leds[i] ? ledmode : PCA9532_LEDMODE_OFF
		);
		params->selectors[selector].val |= led_selector;
	}

	for (int i = 0; i < MAX_LED_SELECTORS; ++i)
	{
		printf("Register %x hertzing for " BYTE_TO_BINARY_PATTERN "\n", params->selectors[i].reg, BYTE_TO_BINARY(params->selectors[i].val));
	}

	return true;
}

bool prepare_device(int* device_handle_ptr)
{
	*device_handle_ptr = open(DEVICE, O_RDWR);
	if (!VALID_DEVICE_HANDLE(*device_handle_ptr))
	{
		return false;
	}

	if (ioctl(*device_handle_ptr, I2C_SLAVE, (__u16)PCA9532_ADDRESS))
	{
		int error_number = errno;
		CLOSE_INVALIDATE(*device_handle_ptr);
		errno = error_number;
		return false;
	}

	atexit(cleanup);
	return true;
}

void prepare_leds(int* device_handle_ptr, struct Parameters* params)
{
	i2c_smbus_write_byte_data(*device_handle_ptr, PCA9532_REGISTER_PWM0, params->duty_cycle);
}

void process_leds(int* device_handle_ptr, struct Parameters* params)
{
	bool turn_on = true;
	do
	{
		for (int i = 0; i < MAX_LED_SELECTORS; ++i)
		{
			i2c_smbus_write_byte_data(*device_handle_ptr, params->selectors[i].reg, 
				turn_on ? 
				params->selectors[i].val : 
				PCA9532_LEDMODE_OFF
			);
		}

		turn_on ^= 1;

		nanosleep(&params->frequency_spec, NULL);
	} while (params->frequency != 0 && stop == 0);
}

int main(int argc, char* argv[])
{
	struct Parameters params;
	memset(&params, 0, sizeof(params));
	if (!parse_arguments(argc, argv, &params))
	{
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (!prepare_device(&device_handle))
	{
		perror(strerror(errno));
		exit(EXIT_FAILURE);
	}

	signal(SIGINT, inthand);
	signal(SIGTERM, inthand);

	prepare_leds(&device_handle, &params);
	process_leds(&device_handle, &params);

	CLOSE_INVALIDATE(device_handle);

    return 0;
}