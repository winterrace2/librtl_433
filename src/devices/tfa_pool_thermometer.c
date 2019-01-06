/* TFA pool temperature sensor
 *
 * Copyright (C) 2015 Alexandre Coffignal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "decoder.h"

static int pool_temperature_sensor_callback(r_device *decoder, bitbuffer_t *bitbuffer, extdata_t *ext) {
	bitrow_t *bb = bitbuffer->bb;
	data_t *data;
	int i,device,channel;
	int iTemp;
	float fTemp;

	for(i=1;i<8;i++){
		if(bitbuffer->bits_per_row[i]!=28){
			/*10 24 bits frame*/
			return 0;
		}
	}

/*
AAAABBBB BBBBCCCC CCCCCCCC DDEE

A: ?
B: device id (changing only after reset)
C: temperature
D: channel number
E: ?
*/

	device=(((bb[1][0]&0xF)<<4)+((bb[1][1]&0xF0)>>4));
	iTemp=((bb[1][1]&0xF)<<8)+bb[1][2];
	fTemp=(iTemp > 2048 ? iTemp - 4096 : iTemp) / 10.0;
	channel=(signed short)((bb[1][3]&0xC0)>>6);

	data = data_make(
			"model",			"", 				DATA_STRING, 	"TFA pool temperature sensor",
			"id",				"Id",				DATA_INT,	device,
			"channel",			"Channel",			DATA_INT,	channel,
			"temperature_C",	"Temperature",		DATA_FORMAT, 	"%.01f C",	DATA_DOUBLE,	fTemp,
		NULL);
    decoder_output_data(decoder, data, ext);

	return 1;

}

static char *output_fields[] = {
	"model",
	"id",
	"channel",
	"temperature_C",
	NULL
};

r_device tfa_pool_thermometer = {
	.name          = "TFA pool temperature sensor",
	.modulation    = OOK_PULSE_PPM,
	.short_width   = 2000,
	.long_width    = 4600,
	.gap_limit     = 7800,
	.reset_limit   = 10000,
	.decode_fn = &pool_temperature_sensor_callback,
	.disabled      = 0,
	.fields        = output_fields,
};
