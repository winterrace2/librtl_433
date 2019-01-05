/*
 * A general structure for extracting hierarchical data from the devices;
 * typically key-value pairs, but allows for more rich data as well.
 *
 * Copyright (C) 2015 by Erkki Seppälä <flux@modeemi.fi>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RTL_433_DATA_PRINTER_CSV_H
#define RTL_433_DATA_PRINTER_CSV_H

#include <stdio.h>

#include "data.h"


 /** Construct data output for CSV printer

 @param file the output stream
 You must release this object with data_output_free once you're done with it.
 */

data_output_t *data_output_csv_create(FILE *file);

/** Setup known field keys and start output, used by CSV only.
@param output the data_output handle from data_output_x_create
@param fields the list of fields to accept and expect. Array is copied, but the actual
strings not. The list may contain duplicates and they are eliminated.
@param num_fields number of fields
*/
void data_output_start(data_output_t *output, const char **fields, int num_fields);

#endif // RTL_433_DATA_PRINTER_CSV_H
