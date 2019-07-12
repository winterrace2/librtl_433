/** @file
    Pulse detection functions.
    Copyright (C) 2015 Tommy Vestermark
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef RTL_433_PULSE_ANALYZE_H
#define RTL_433_PULSE_ANALYZE_H

#include <stdint.h>

#include "librtl_433.h"
#include "pulse_detect.h"

/// Analyze and print result.
void pulse_analyzer(pulse_data_t *data, int package_type, rtl_433_t *ctx);


#endif /* RTL_433_PULSE_ANALYZE_H */
