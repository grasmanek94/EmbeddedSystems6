#include "gpio_common.h"

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
		.MAPPING_PINS = 8,
		.MAPPING =
		{
			{ J3, 40 },
			{ J2, 24 },
			{ J2, 11 },
			{ J2, 12 },
			{ J2, 13 },
			{ J2, 14 },
			{ J3, 33 },
			{ J1, 27 },
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
		.MAPPING_PINS = 13,
		.MAPPING =
		{
			{ J3, 47 },
			{ J3, 56 },
			{ J3, 48 },
			{ J3, 55 },
			{ J3, 49 },
			{ J3, 58 },
			{ J3, 50 },
			{ J3, 45 },
			{ J1, 49 },
			{ J1, 50 },
			{ J1, 51 },
			{ J1, 52 },
			{ J1, 53 }
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
		.MAPPING_PINS = 6,
		.MAPPING =
		{
			{ J3, 54 },
			{ J3, 46 },
			{ -1, -1,},
			{ -1, -1 },
			{ J3, 36 },
			{ J1, 24 },
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
