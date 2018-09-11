#include <asm/div64.h>

#include "pwm_common.h"

uint32_t* PWM_ControlRegisterAddresses[MAX_PWMS] =
{
	io_p2v(0x4005C000),
	io_p2v(0x4005C004)
};

uint32_t* PWM_Clock_Address = io_p2v(0x400040B8);

int64_t abslt(int64_t val)
{	// because there is not 64bit abs standard in C unless long is by default 64bit (which is not always the case)
	return val < 0 ? -val : val;
}

struct ClockCalculation get_optimal_clocks(int64_t frequency_millihertz)
{
	// we could do some fancy calculations here,
	// but due to time constraints we just brute force the best approximation.
	// if you feel adventureous, go ahead and improve it

	enum TESTS
	{
		RTC,
		PERIPH,
		MAX_TESTS
	};

	struct ClockCalculation tests[MAX_TESTS];
	int freq;
	int reloadv;
	int reloadv_normalized;
	int t;
	int64_t smallest_dist;
	int64_t freq_x_reloadv;
	int64_t distance;
	int64_t temp;

	int smallest_selector = 0;

	if (frequency_millihertz < 0)
	{
		frequency_millihertz *= -1;
	}

	smallest_dist = frequency_millihertz;

	tests[RTC].source = CLOCK_SOURCE_RTC;
	tests[RTC].max = FREQ_LOW * HERTZ_TO_MILLIHERTZ;
	tests[RTC].distance = frequency_millihertz;

	tests[PERIPH].source = CLOCK_SOURCE_PERIPH;
	tests[PERIPH].max = FREQ_HIGH * HERTZ_TO_MILLIHERTZ;
	tests[PERIPH].distance = frequency_millihertz;

	for (freq = MIN_FREQ_REGISTER; freq < MAX_FREQ_REGISTER; ++freq)
	{
		for (reloadv = MIN_RELOADV_REGISTER; reloadv < MAX_RELOADV_REGISTER; ++reloadv)
		{
			reloadv_normalized = (reloadv == 0) ? MAX_RELOADV_REGISTER : reloadv;
			freq_x_reloadv = reloadv_normalized * freq;

			for (t = 0; t < MAX_TESTS; ++t)
			{
				temp = frequency_millihertz;
				do_div(temp, freq_x_reloadv);
				distance = abslt(frequency_millihertz - (temp * freq_x_reloadv));
				if (distance < tests[t].distance)
				{
					tests[t].distance = distance;
					tests[t].freq = freq;
					tests[t].reloadv = reloadv;

					if (distance < smallest_dist)
					{
						smallest_selector = t;
					}
				}
			}
		}
	}

	return tests[smallest_selector];
}

void set_pwm_frequency(int pwm)
{
	union PWM_Control control;
	union PWM_Clock clock;
	struct ClockCalculation optimal;

	control.value = ioread32(PWM_ControlRegisterAddresses[pwm]);
	clock.value = ioread32(PWM_Clock_Address);

	optimal = get_optimal_clocks(simple_strtoll(kernel_buffer, 0, 0));

	switch (pwm)
	{
	case PWM1:
		clock.clock.PWM1_CLOCKSRC = optimal.source;
		clock.clock.PWM1_FREQ = optimal.freq;
		break;
	case PWM2:
		clock.clock.PWM2_CLOCKSRC = optimal.source;
		clock.clock.PWM2_FREQ = optimal.freq;
		break;
	default:
		// nothing to do here
		break;
	}

	control.control.PWM_ReloadV = optimal.reloadv;

	iowrite32(control.value, PWM_ControlRegisterAddresses[pwm]);
	iowrite32(clock.value, PWM_Clock_Address);
}

void set_pwm_duty(int pwm)
{
	union PWM_Control control;

	control.value = ioread32(PWM_ControlRegisterAddresses[pwm]);

	control.control.PWM_Duty = ((char)(simple_strtoul(kernel_buffer, 0, 0) * 255 / 100)) & 0xFF;

	iowrite32(control.value, PWM_ControlRegisterAddresses[pwm]);
}

void set_pwm_enabled(int pwm)
{
	union PWM_Control control;

	control.value = ioread32(PWM_ControlRegisterAddresses[pwm]);

	control.control.PWM_Enabled = simple_strtol(kernel_buffer, 0, 0) != 0;

	iowrite32(control.value, PWM_ControlRegisterAddresses[pwm]);
}

void get_pwm_frequency(int pwm)
{
	union PWM_Control control;
	union PWM_Clock clock;

	int64_t calculated_frequency = 0;
	int64_t base_frequency = 0;
	int64_t freq = 0;
	int64_t freq_x_reloadv = 0;
	int64_t reload_v = 256;

	control.value = ioread32(PWM_ControlRegisterAddresses[pwm]);
	clock.value = ioread32(PWM_Clock_Address);

	switch (pwm)
	{
	case PWM1:
		if (clock.clock.PWM1_CLOCKSRC == CLOCK_SOURCE_RTC)
		{
			base_frequency = FREQ_LOW;
		}
		else if(clock.clock.PWM1_CLOCKSRC == CLOCK_SOURCE_PERIPH)
		{
			base_frequency = FREQ_HIGH;
		}
		else
		{
			// nothing to do here
		}
		freq = clock.clock.PWM1_FREQ;
		break;
	case PWM2:
		if (clock.clock.PWM2_CLOCKSRC == CLOCK_SOURCE_RTC)
		{
			base_frequency = FREQ_LOW;
		}
		else if (clock.clock.PWM2_CLOCKSRC == CLOCK_SOURCE_PERIPH)
		{
			base_frequency = FREQ_HIGH;
		}
		else
		{
			// nothing to do here
		}
		freq = clock.clock.PWM1_FREQ;
		break;
	default:
		// nothing to do here
		break;
	}

	if (control.control.PWM_ReloadV != 0)
	{
		reload_v = control.control.PWM_ReloadV;
	}

	freq_x_reloadv = (freq * reload_v);
	if (freq_x_reloadv != 0)
	{
		calculated_frequency = base_frequency * HERTZ_TO_MILLIHERTZ;
		do_div(calculated_frequency, freq_x_reloadv);
	}

	sprintf(kernel_buffer, "%lu", (unsigned long)calculated_frequency);
	kernel_buffer_length = strlen(kernel_buffer);
}

void get_pwm_duty(int pwm)
{
	union PWM_Control control;

	control.value = ioread32(PWM_ControlRegisterAddresses[pwm]);

	sprintf(kernel_buffer, "%d", (int)(control.control.PWM_Duty * 100 / 255));
	kernel_buffer_length = strlen(kernel_buffer);
}

void get_pwm_enabled(int pwm)
{
	union PWM_Control control;

	control.value = ioread32(PWM_ControlRegisterAddresses[pwm]);

	sprintf(kernel_buffer, "%d", (int)control.control.PWM_Enabled);
	kernel_buffer_length = strlen(kernel_buffer);
}
