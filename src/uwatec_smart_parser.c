/*
 * libdivecomputer
 *
 * Copyright (C) 2008 Jef Driesen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#include <stdlib.h>
#include <string.h>	// memcmp

#include <libdivecomputer/uwatec_smart.h>
#include <libdivecomputer/units.h>

#include "context-private.h"
#include "parser-private.h"
#include "array.h"

#define ISINSTANCE(parser) dc_parser_isinstance((parser), &uwatec_smart_parser_vtable)

#define C_ARRAY_SIZE(array) (sizeof (array) / sizeof *(array))

#define NBITS 8

#define SMARTPRO      0x10
#define GALILEO       0x11
#define ALADINTEC     0x12
#define ALADINTEC2G   0x13
#define SMARTCOM      0x14
#define ALADIN2G      0x15
#define SMARTTEC      0x18
#define GALILEOTRIMIX 0x19
#define SMARTZ        0x1C
#define MERIDIAN      0x20

typedef enum {
	PRESSURE_DEPTH,
	RBT,
	TEMPERATURE,
	PRESSURE,
	DEPTH,
	HEARTRATE,
	BEARING,
	ALARMS,
	TIME,
	UNKNOWN1,
	UNKNOWN2,
} uwatec_smart_sample_t;

typedef struct uwatec_smart_header_info_t {
	unsigned int maxdepth;
	unsigned int divetime;
	unsigned int gasmix;
	unsigned int ngases;
} uwatec_smart_header_info_t;

typedef struct uwatec_smart_sample_info_t {
	uwatec_smart_sample_t type;
	unsigned int absolute;
	unsigned int index;
	unsigned int ntypebits;
	unsigned int ignoretype;
	unsigned int extrabytes;
} uwatec_smart_sample_info_t;

typedef struct uwatec_smart_parser_t uwatec_smart_parser_t;

struct uwatec_smart_parser_t {
	dc_parser_t base;
	unsigned int model;
	unsigned int devtime;
	dc_ticks_t systime;
	const uwatec_smart_sample_info_t *samples;
	const uwatec_smart_header_info_t *header;
	unsigned int headersize;
	unsigned int nsamples;
};

static dc_status_t uwatec_smart_parser_set_data (dc_parser_t *abstract, const unsigned char *data, unsigned int size);
static dc_status_t uwatec_smart_parser_get_datetime (dc_parser_t *abstract, dc_datetime_t *datetime);
static dc_status_t uwatec_smart_parser_get_field (dc_parser_t *abstract, dc_field_type_t type, unsigned int flags, void *value);
static dc_status_t uwatec_smart_parser_samples_foreach (dc_parser_t *abstract, dc_sample_callback_t callback, void *userdata);
static dc_status_t uwatec_smart_parser_destroy (dc_parser_t *abstract);

static const dc_parser_vtable_t uwatec_smart_parser_vtable = {
	DC_FAMILY_UWATEC_SMART,
	uwatec_smart_parser_set_data, /* set_data */
	uwatec_smart_parser_get_datetime, /* datetime */
	uwatec_smart_parser_get_field, /* fields */
	uwatec_smart_parser_samples_foreach, /* samples_foreach */
	uwatec_smart_parser_destroy /* destroy */
};

static const
uwatec_smart_header_info_t uwatec_smart_pro_header = {
	18,
	20,
	24, 1
};

static const
uwatec_smart_header_info_t uwatec_smart_galileo_header = {
	22,
	26,
	44, 3
};

static const
uwatec_smart_header_info_t uwatec_smart_aladin_tec_header = {
	22,
	24,
	30, 1
};

static const
uwatec_smart_header_info_t uwatec_smart_aladin_tec2g_header = {
	22,
	26,
	34, 3
};

static const
uwatec_smart_header_info_t uwatec_smart_com_header = {
	18,
	20,
	24, 1
};

static const
uwatec_smart_header_info_t uwatec_smart_tec_header = {
	18,
	20,
	28, 3
};

static const
uwatec_smart_sample_info_t uwatec_smart_pro_samples[] = {
	{DEPTH,          0, 0, 1, 0, 0}, // 0ddddddd
	{TEMPERATURE,    0, 0, 2, 0, 0}, // 10dddddd
	{TIME,           1, 0, 3, 0, 0}, // 110ddddd
	{ALARMS,         1, 0, 4, 0, 0}, // 1110dddd
	{DEPTH,          0, 0, 5, 0, 1}, // 11110ddd dddddddd
	{TEMPERATURE,    0, 0, 6, 0, 1}, // 111110dd dddddddd
	{DEPTH,          1, 0, 7, 1, 2}, // 1111110d dddddddd dddddddd
	{TEMPERATURE,    1, 0, 8, 0, 2}, // 11111110 dddddddd dddddddd
};

static const
uwatec_smart_sample_info_t uwatec_smart_galileo_samples[] = {
	{DEPTH,          0, 0, 1, 0, 0}, // 0ddd dddd
	{RBT,            0, 0, 3, 0, 0}, // 100d dddd
	{PRESSURE,       0, 0, 4, 0, 0}, // 1010 dddd
	{TEMPERATURE,    0, 0, 4, 0, 0}, // 1011 dddd
	{TIME,           1, 0, 4, 0, 0}, // 1100 dddd
	{HEARTRATE,      0, 0, 4, 0, 0}, // 1101 dddd
	{ALARMS,         1, 0, 4, 0, 0}, // 1110 dddd
	{ALARMS,         1, 1, 8, 0, 1}, // 1111 0000 dddddddd
	{DEPTH,          1, 0, 8, 0, 2}, // 1111 0001 dddddddd dddddddd
	{RBT,            1, 0, 8, 0, 1}, // 1111 0010 dddddddd
	{TEMPERATURE,    1, 0, 8, 0, 2}, // 1111 0011 dddddddd dddddddd
	{PRESSURE,       1, 0, 8, 0, 2}, // 1111 0100 dddddddd dddddddd
	{PRESSURE,       1, 1, 8, 0, 2}, // 1111 0101 dddddddd dddddddd
	{PRESSURE,       1, 2, 8, 0, 2}, // 1111 0110 dddddddd dddddddd
	{HEARTRATE,      1, 0, 8, 0, 1}, // 1111 0111 dddddddd
	{BEARING,        1, 0, 8, 0, 2}, // 1111 1000 dddddddd dddddddd
	{ALARMS,         1, 2, 8, 0, 1}, // 1111 1001 dddddddd
	{UNKNOWN1,       1, 0, 8, 0, 0}, // 1111 1010 (8 bytes)
	{UNKNOWN2,       1, 0, 8, 0, 1}, // 1111 1011 dddddddd (n-1 bytes)
};


static const
uwatec_smart_sample_info_t uwatec_smart_aladin_samples[] = {
	{DEPTH,          0, 0, 1, 0, 0}, // 0ddddddd
	{TEMPERATURE,    0, 0, 2, 0, 0}, // 10dddddd
	{TIME,           1, 0, 3, 0, 0}, // 110ddddd
	{ALARMS,         1, 0, 4, 0, 0}, // 1110dddd
	{DEPTH,          0, 0, 5, 0, 1}, // 11110ddd dddddddd
	{TEMPERATURE,    0, 0, 6, 0, 1}, // 111110dd dddddddd
	{DEPTH,          1, 0, 7, 1, 2}, // 1111110d dddddddd dddddddd
	{TEMPERATURE,    1, 0, 8, 0, 2}, // 11111110 dddddddd dddddddd
	{ALARMS,         1, 1, 9, 0, 0}, // 11111111 0ddddddd
};

static const
uwatec_smart_sample_info_t uwatec_smart_com_samples[] = {
	{PRESSURE_DEPTH, 0, 0,  1, 0, 1}, // 0ddddddd dddddddd
	{RBT,            0, 0,  2, 0, 0}, // 10dddddd
	{TEMPERATURE,    0, 0,  3, 0, 0}, // 110ddddd
	{PRESSURE,       0, 0,  4, 0, 1}, // 1110dddd dddddddd
	{DEPTH,          0, 0,  5, 0, 1}, // 11110ddd dddddddd
	{TEMPERATURE,    0, 0,  6, 0, 1}, // 111110dd dddddddd
	{ALARMS,         1, 0,  7, 1, 1}, // 1111110d dddddddd
	{TIME,           1, 0,  8, 0, 1}, // 11111110 dddddddd
	{DEPTH,          1, 0,  9, 1, 2}, // 11111111 0ddddddd dddddddd dddddddd
	{PRESSURE,       1, 0, 10, 1, 2}, // 11111111 10dddddd dddddddd dddddddd
	{TEMPERATURE,    1, 0, 11, 1, 2}, // 11111111 110ddddd dddddddd dddddddd
	{RBT,            1, 0, 12, 1, 1}, // 11111111 1110dddd dddddddd
};

static const
uwatec_smart_sample_info_t uwatec_smart_tec_samples[] = {
	{PRESSURE_DEPTH, 0, 0,  1, 0, 1}, // 0ddddddd dddddddd
	{RBT,            0, 0,  2, 0, 0}, // 10dddddd
	{TEMPERATURE,    0, 0,  3, 0, 0}, // 110ddddd
	{PRESSURE,       0, 0,  4, 0, 1}, // 1110dddd dddddddd
	{DEPTH,          0, 0,  5, 0, 1}, // 11110ddd dddddddd
	{TEMPERATURE,    0, 0,  6, 0, 1}, // 111110dd dddddddd
	{ALARMS,         1, 0,  7, 1, 1}, // 1111110d dddddddd
	{TIME,           1, 0,  8, 0, 1}, // 11111110 dddddddd
	{DEPTH,          1, 0,  9, 1, 2}, // 11111111 0ddddddd dddddddd dddddddd
	{TEMPERATURE,    1, 0, 10, 1, 2}, // 11111111 10dddddd dddddddd dddddddd
	{PRESSURE,       1, 0, 11, 1, 2}, // 11111111 110ddddd dddddddd dddddddd
	{PRESSURE,       1, 1, 12, 1, 2}, // 11111111 1110dddd dddddddd dddddddd
	{PRESSURE,       1, 2, 13, 1, 2}, // 11111111 11110ddd dddddddd dddddddd
	{RBT,            1, 0, 14, 1, 1}, // 11111111 111110dd dddddddd
};


dc_status_t
uwatec_smart_parser_create (dc_parser_t **out, dc_context_t *context, unsigned int model, unsigned int devtime, dc_ticks_t systime)
{
	if (out == NULL)
		return DC_STATUS_INVALIDARGS;

	// Allocate memory.
	uwatec_smart_parser_t *parser = (uwatec_smart_parser_t *) malloc (sizeof (uwatec_smart_parser_t));
	if (parser == NULL) {
		ERROR (context, "Failed to allocate memory.");
		return DC_STATUS_NOMEMORY;
	}

	// Initialize the base class.
	parser_init (&parser->base, context, &uwatec_smart_parser_vtable);

	// Set the default values.
	parser->model = model;
	parser->devtime = devtime;
	parser->systime = systime;
	switch (model) {
	case SMARTPRO:
		parser->headersize = 92;
		parser->header = &uwatec_smart_pro_header;
		parser->samples = uwatec_smart_pro_samples;
		parser->nsamples = C_ARRAY_SIZE (uwatec_smart_pro_samples);
		break;
	case GALILEO:
	case GALILEOTRIMIX:
	case ALADIN2G:
	case MERIDIAN:
		parser->headersize = 152;
		parser->header = &uwatec_smart_galileo_header;
		parser->samples = uwatec_smart_galileo_samples;
		parser->nsamples = C_ARRAY_SIZE (uwatec_smart_galileo_samples);
		break;
	case ALADINTEC:
		parser->headersize = 108;
		parser->header = &uwatec_smart_aladin_tec_header;
		parser->samples = uwatec_smart_aladin_samples;
		parser->nsamples = C_ARRAY_SIZE (uwatec_smart_aladin_samples);
		break;
	case ALADINTEC2G:
		parser->headersize = 116;
		parser->header = &uwatec_smart_aladin_tec2g_header;
		parser->samples = uwatec_smart_aladin_samples;
		parser->nsamples = C_ARRAY_SIZE (uwatec_smart_aladin_samples);
		break;
	case SMARTCOM:
		parser->headersize = 100;
		parser->header = &uwatec_smart_com_header;
		parser->samples = uwatec_smart_com_samples;
		parser->nsamples = C_ARRAY_SIZE (uwatec_smart_com_samples);
		break;
	case SMARTTEC:
	case SMARTZ:
		parser->headersize = 132;
		parser->header = &uwatec_smart_tec_header;
		parser->samples = uwatec_smart_tec_samples;
		parser->nsamples = C_ARRAY_SIZE (uwatec_smart_tec_samples);
		break;
	default:
		free (parser);
		return DC_STATUS_INVALIDARGS;
	}

	*out = (dc_parser_t*) parser;

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_smart_parser_destroy (dc_parser_t *abstract)
{
	// Free memory.
	free (abstract);

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_smart_parser_set_data (dc_parser_t *abstract, const unsigned char *data, unsigned int size)
{
	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_smart_parser_get_datetime (dc_parser_t *abstract, dc_datetime_t *datetime)
{
	uwatec_smart_parser_t *parser = (uwatec_smart_parser_t *) abstract;

	if (abstract->size < 8 + 4)
		return DC_STATUS_DATAFORMAT;

	unsigned int timestamp = array_uint32_le (abstract->data + 8);

	dc_ticks_t ticks = parser->systime - (parser->devtime - timestamp) / 2;

	if (!dc_datetime_localtime (datetime, ticks))
		return DC_STATUS_DATAFORMAT;

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_smart_parser_get_field (dc_parser_t *abstract, dc_field_type_t type, unsigned int flags, void *value)
{
	uwatec_smart_parser_t *parser = (uwatec_smart_parser_t *) abstract;

	const unsigned char *data = abstract->data;
	unsigned int size = abstract->size;

	const uwatec_smart_header_info_t *table = parser->header;
	unsigned int header = parser->headersize;
	unsigned int trimix = 0;

	if (parser->model == GALILEOTRIMIX) {
		if (size < 44)
			return DC_STATUS_DATAFORMAT;

		if (data[43] & 0x80) {
			header = 0xB1;
			trimix = 1;
		}
	}

	if (size < header)
		return DC_STATUS_DATAFORMAT;

	dc_gasmix_t *gasmix = (dc_gasmix_t *) value;

	if (value) {
		switch (type) {
		case DC_FIELD_DIVETIME:
			*((unsigned int *) value) = array_uint16_le (data + table->divetime) * 60;
			break;
		case DC_FIELD_MAXDEPTH:
			*((double *) value) = array_uint16_le (data + table->maxdepth) / 100.0;
			break;
		case DC_FIELD_GASMIX_COUNT:
			if (trimix)
				*((unsigned int *) value) = 0;
			else
				*((unsigned int *) value) = table->ngases;
			break;
		case DC_FIELD_GASMIX:
			gasmix->helium = 0.0;
			gasmix->oxygen = data[table->gasmix + flags * 2] / 100.0;
			gasmix->nitrogen = 1.0 - gasmix->oxygen - gasmix->helium;
			break;
		default:
			return DC_STATUS_UNSUPPORTED;
		}
	}

	return DC_STATUS_SUCCESS;
}


static unsigned int
uwatec_smart_identify (const unsigned char data[], unsigned int size)
{
	unsigned int count = 0;
	for (unsigned int i = 0; i < size; ++i) {
		unsigned char value = data[i];
		for (unsigned int j = 0; j < NBITS; ++j) {
			unsigned char mask = 1 << (NBITS - 1 - j);
			if ((value & mask) == 0)
				return count;
			count++;
		}
	}

	return (unsigned int) -1;
}


static unsigned int
uwatec_galileo_identify (unsigned char value)
{
	// Bits: 0ddd dddd
	if ((value & 0x80) == 0)
		return 0;

	// Bits: 100d dddd
	if ((value & 0xE0) == 0x80)
		return 1;

	// Bits: 1XXX dddd
	if ((value & 0xF0) != 0xF0)
		return (value & 0x70) >> 4;

	// Bits: 1111 XXXX
	return (value & 0x0F) + 7;
}


static unsigned int
uwatec_smart_fixsignbit (unsigned int x, unsigned int n)
{
	if (n <= 0 || n > 32)
		return 0;

	unsigned int signbit = (1 << (n - 1));
	unsigned int mask = (0xFFFFFFFF << n);

	// When turning a two's-complement number with a certain number
	// of bits into one with more bits, the sign bit must be repeated
	// in all the extra bits.
	if ((x & signbit) == signbit)
		return x | mask;
	else
		return x & ~mask;
}


static dc_status_t
uwatec_smart_parser_samples_foreach (dc_parser_t *abstract, dc_sample_callback_t callback, void *userdata)
{
	uwatec_smart_parser_t *parser = (uwatec_smart_parser_t*) abstract;

	const unsigned char *data = abstract->data;
	unsigned int size = abstract->size;

	const uwatec_smart_sample_info_t *table = parser->samples;
	unsigned int entries = parser->nsamples;
	unsigned int header = parser->headersize;
	unsigned int trimix = 0;

	if (parser->model == GALILEOTRIMIX) {
		if (size < 44)
			return DC_STATUS_DATAFORMAT;

		if (data[43] & 0x80) {
			header = 0xB1;
			trimix = 1;
		}
	}

	// Get the maximum number of alarm bytes.
	unsigned int nalarms = 0;
	for (unsigned int i = 0; i < entries; ++i) {
		if (table[i].type == ALARMS &&
			table[i].index >= nalarms)
		{
			nalarms = table[i].index + 1;
		}
	}

	// Get all the gas mixes.
	unsigned int oxygen[3] = {0};
	unsigned int ngasmix = (trimix ? 0 : parser->header->ngases);
	for (unsigned int i = 0; i < ngasmix; ++i) {
		oxygen[i] = data[parser->header->gasmix + i * 2];
	}

	int complete = 0;
	int calibrated = 0;

	unsigned int time = 0;
	unsigned int rbt = 99;
	unsigned int tank = 0;
	unsigned int gasmix = 0;
	double depth = 0, depth_calibration = 0;
	double temperature = 0;
	double pressure = 0;
	unsigned int heartrate = 0;
	unsigned int bearing = 0;
	unsigned char alarms[3] = {0, 0, 0};

	// Previous gas mix - initialize with impossible value
	unsigned int gasmix_previous = 0xFFFFFFFF;

	int have_depth = 0, have_temperature = 0, have_pressure = 0, have_rbt = 0,
		have_heartrate = 0, have_alarms = 0, have_bearing = 0;

	unsigned int offset = header;
	while (offset < size) {
		dc_sample_value_t sample = {0};

		// Process the type bits in the bitstream.
		unsigned int id = 0;
		if (parser->model == GALILEO || parser->model == GALILEOTRIMIX ||
			parser->model == ALADIN2G || parser->model == MERIDIAN) {
			// Uwatec Galileo
			id = uwatec_galileo_identify (data[offset]);
		} else {
			// Uwatec Smart
			id = uwatec_smart_identify (data + offset, size - offset);
		}
		if (id >= entries) {
			ERROR (abstract->context, "Invalid type bits.");
			return DC_STATUS_DATAFORMAT;
		}

		// Skip the processed type bytes.
		offset += table[id].ntypebits / NBITS;

		// Process the remaining data bits.
		unsigned int nbits = 0;
		unsigned int value = 0;
		unsigned int n = table[id].ntypebits % NBITS;
		if (n > 0) {
			nbits = NBITS - n;
			value = data[offset] & (0xFF >> n);
			if (table[id].ignoretype) {
				// Ignore any data bits that are stored in
				// the last type byte for certain samples.
				nbits = 0;
				value = 0;
			}
			offset++;
		}

		// Check for buffer overflows.
		if (offset + table[id].extrabytes > size) {
			ERROR (abstract->context, "Incomplete sample data.");
			return DC_STATUS_DATAFORMAT;
		}

		// Process the extra data bytes.
		for (unsigned int i = 0; i < table[id].extrabytes; ++i) {
			nbits += NBITS;
			value <<= NBITS;
			value += data[offset];
			offset++;
		}

		// Fix the sign bit.
		signed int svalue = uwatec_smart_fixsignbit (value, nbits);

		// Parse the value.
		switch (table[id].type) {
		case PRESSURE_DEPTH:
			pressure += ((signed char) ((svalue >> NBITS) & 0xFF)) / 4.0;
			depth += ((signed char) (svalue & 0xFF)) / 50.0;
			complete = 1;
			break;
		case RBT:
			if (table[id].absolute) {
				rbt = value;
				have_rbt = 1;
			} else {
				rbt += svalue;
			}
			break;
		case TEMPERATURE:
			if (table[id].absolute) {
				temperature = svalue / 2.5;
				have_temperature = 1;
			} else {
				temperature += svalue / 2.5;
			}
			break;
		case PRESSURE:
			if (table[id].absolute) {
				if (trimix) {
					tank = (value & 0xF000) >> 24;
					pressure = (value & 0x0FFF) / 4.0;
				} else {
					tank = table[id].index;
					pressure = value / 4.0;
				}
				have_pressure = 1;
				gasmix = tank;
			} else {
				pressure += svalue / 4.0;
			}
			break;
		case DEPTH:
			if (table[id].absolute) {
				depth = value / 50.0;
				if (!calibrated) {
					calibrated = 1;
					depth_calibration = depth;
				}
				have_depth = 1;
			} else {
				depth += svalue / 50.0;
			}
			complete = 1;
			break;
		case HEARTRATE:
			if (table[id].absolute) {
				heartrate = value;
				have_heartrate = 1;
			} else {
				heartrate += svalue;
			}
			break;
		case BEARING:
			bearing = value;
			have_bearing = 1;
			break;
		case ALARMS:
			alarms[table[id].index] = value;
			have_alarms = 1;
			if (table[id].index == 1) {
				if (parser->model == ALADINTEC || parser->model == ALADINTEC2G) {
					gasmix = (value & 0x18) >> 3;
				} else {
					gasmix = (value & 0x30) >> 4;
				}
			}
			break;
		case TIME:
			complete = value;
			break;
		case UNKNOWN1:
			if (offset + 8 > size) {
				ERROR (abstract->context, "Incomplete sample data.");
				return DC_STATUS_DATAFORMAT;
			}
			offset += 8;
			break;
		case UNKNOWN2:
			if (value < 1 || offset + value - 1 > size) {
				ERROR (abstract->context, "Incomplete sample data.");
				return DC_STATUS_DATAFORMAT;
			}
			offset += value - 1;
			break;
		default:
			WARNING (abstract->context, "Unknown sample type.");
			break;
		}

		while (complete) {
			sample.time = time;
			if (callback) callback (DC_SAMPLE_TIME, sample, userdata);

			if (gasmix != gasmix_previous && !trimix) {
				if (gasmix >= ngasmix)
					return DC_STATUS_DATAFORMAT;
				sample.event.type = SAMPLE_EVENT_GASCHANGE;
				sample.event.time = 0;
				sample.event.flags = 0;
				sample.event.value = oxygen[gasmix];
				if (callback) callback (DC_SAMPLE_EVENT, sample, userdata);
				gasmix_previous = gasmix;
			}

			if (have_temperature) {
				sample.temperature = temperature;
				if (callback) callback (DC_SAMPLE_TEMPERATURE, sample, userdata);
			}

			if (have_alarms) {
				sample.vendor.type = SAMPLE_VENDOR_UWATEC_SMART;
				sample.vendor.size = nalarms;
				sample.vendor.data = alarms;
				if (callback) callback (DC_SAMPLE_VENDOR, sample, userdata);
				memset (alarms, 0, sizeof (alarms));
				have_alarms = 0;
			}

			if (have_rbt || have_pressure) {
				sample.rbt = rbt;
				if (callback) callback (DC_SAMPLE_RBT, sample, userdata);
			}

			if (have_pressure) {
				sample.pressure.tank = tank;
				sample.pressure.value = pressure;
				if (callback) callback (DC_SAMPLE_PRESSURE, sample, userdata);
			}

			if (have_heartrate) {
				sample.heartbeat = heartrate;
				if (callback) callback (DC_SAMPLE_HEARTBEAT, sample, userdata);
			}

			if (have_bearing) {
				sample.bearing = bearing;
				if (callback) callback (DC_SAMPLE_BEARING, sample, userdata);
				have_bearing = 0;
			}

			if (have_depth) {
				sample.depth = depth - depth_calibration;
				if (callback) callback (DC_SAMPLE_DEPTH, sample, userdata);
			}

			time += 4;
			complete--;
		}
	}

	return DC_STATUS_SUCCESS;
}
