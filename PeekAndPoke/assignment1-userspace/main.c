#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

int main()
{
	volatile uint32_t* rtc_upcounter = (uint32_t*)0x40024000;

	uint32_t a = *rtc_upcounter;
	usleep(10 * 1000 * 1000); // sleep 10 seconds (10 million microseconds)
	uint32_t b = *rtc_upcounter;

	printf("a: %d, b: %d, difference(b-a): %d", a, b, b - a);

	return 0;
}
