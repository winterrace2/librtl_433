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

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "redir_print.h"
#include "abuf.h"
#include "data_printer_jsonstr.h"
 
/* JSON string printer */

typedef struct {
    data_output_t output;
    abuf_t msg;
} data_print_jsons_t;

static void format_jsons_array(data_output_t *output, data_array_t *array, char *format)
{
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;

    abuf_cat(&jsons->msg, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            abuf_cat(&jsons->msg, ",");
        print_array_value(output, array, format, c);
    }
    abuf_cat(&jsons->msg, "]");
}

static void format_jsons_object(data_output_t *output, data_t *data, char *format)
{
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;

    bool separator = false;
    abuf_cat(&jsons->msg, "{");
    while (data) {
        if (separator)
            abuf_cat(&jsons->msg, ",");
        output->print_string(output, data->key, NULL);
        abuf_cat(&jsons->msg, ":");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data      = data->next;
    }
    abuf_cat(&jsons->msg, "}");
}

static void format_jsons_string(data_output_t *output, const char *str, char *format)
{
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;

    char *buf   = jsons->msg.tail;
    size_t size = jsons->msg.left;

    if (size < strlen(str) + 3) {
        return;
    }

    *buf++ = '"';
    size--;
    for (; *str && size >= 3; ++str) {
        if (*str == '"' || *str == '\\') {
            *buf++ = '\\';
            size--;
        }
        *buf++ = *str;
        size--;
    }
    if (size >= 2) {
        *buf++ = '"';
        size--;
    }
    *buf = '\0';

    jsons->msg.tail = buf;
    jsons->msg.left = size;
}

static void format_jsons_double(data_output_t *output, double data, char *format)
{
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;
    // use scientific notation for very big/small values
    if (data > 1e7 || data < 1e-4) {
        abuf_printf(&jsons->msg, "%g", data);
    }
    else {
        abuf_printf(&jsons->msg, "%.5f", data);
        // remove trailing zeros, always keep one digit after the decimal point
        while (jsons->msg.left > 0 && *(jsons->msg.tail - 1) == '0' && *(jsons->msg.tail - 2) != '.') {
            jsons->msg.tail--;
            jsons->msg.left++;
            *jsons->msg.tail = '\0';
        }
    }
}

static void format_jsons_int(data_output_t *output, int data, char *format)
{
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;
    abuf_printf(&jsons->msg, "%d", data);
}

size_t data_print_jsons(data_t *data, char *dst, size_t len)
{
    data_print_jsons_t jsons = {
            .output.print_data   = format_jsons_object,
            .output.print_array  = format_jsons_array,
            .output.print_string = format_jsons_string,
            .output.print_double = format_jsons_double,
            .output.print_int    = format_jsons_int,
    };

    abuf_init(&jsons.msg, dst, len);

    format_jsons_object(&jsons.output, data, NULL);

    return len - jsons.msg.left;
}
