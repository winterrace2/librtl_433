#ifndef RTL_433_DATA_PRINTER_EXT_H
#define RTL_433_DATA_PRINTER_EXT_H

#include <stdio.h>
#include "pulse_detect.h"
#include "data.h"
#include "bitbuffer.h"

typedef struct _extdata {
	bitbuffer_t *bitbuffer;
	const pulse_data_t *pulses;
	unsigned pulseexc_startidx;
	unsigned pulseexc_len;
	unsigned mod;
	unsigned samprate;
} extdata_t;

typedef struct data_ext {
	data_t      data;
	extdata_t	ext;
} data_ext_t;

typedef void(*rx_callback)(data_ext_t *datext);

data_output_t *data_output_extcb_create(void *cb);

#endif // RTL_433_DATA_PRINTER_EXT_H
