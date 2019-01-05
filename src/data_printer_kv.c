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
#include <string.h>
#include <stdbool.h>
#include "redir_print.h"
#include "data_printer_kv.h"
#include "term_ctl.h"

/* Pretty Key-Value printer */

static int kv_color_for_key(char const *key)
{
    if (!key || !*key)
        return TERM_COLOR_RESET;
    if (!strcmp(key, "tag") || !strcmp(key, "time"))
        return TERM_COLOR_BLUE;
    if (!strcmp(key, "model") || !strcmp(key, "type") || !strcmp(key, "id"))
        return TERM_COLOR_RED;
    if (!strcmp(key, "mic"))
        return TERM_COLOR_CYAN;
    if (!strcmp(key, "mod") || !strcmp(key, "freq") || !strcmp(key, "freq1") || !strcmp(key, "freq2"))
        return TERM_COLOR_MAGENTA;
    if (!strcmp(key, "rssi") || !strcmp(key, "snr") || !strcmp(key, "noise"))
        return TERM_COLOR_YELLOW;
    return TERM_COLOR_GREEN;
}

static int kv_break_before_key(char const *key)
{
    if (!key || !*key)
        return 0;
    if (!strcmp(key, "model") || !strcmp(key, "mod") || !strcmp(key, "rssi") || !strcmp(key, "codes"))
        return 1;
    return 0;
}

static int kv_break_after_key(char const *key)
{
    if (!key || !*key)
        return 0;
    if (!strcmp(key, "id") || !strcmp(key, "mic"))
        return 1;
    return 0;
}

typedef struct {
    data_output_t output;
	void *term;
	int color;
    int ring_bell;
    int term_width;
    int data_recursion;
    int column;
} data_output_kv_t;
 
#define KV_SEP "_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ "

static void print_kv_data(data_output_t *output, data_t *data, char *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;
    
	int color = kv->color;
    int ring_bell = kv->ring_bell;

    // top-level: update width and print separator
    if (!kv->data_recursion) {
        kv->term_width = term_get_columns(kv->term); // update current term width
        if (color)
            term_set_fg(kv->term, TERM_COLOR_BLACK);
        if (ring_bell)
            term_ring_bell(kv->term);
        char sep[] = KV_SEP KV_SEP KV_SEP KV_SEP;
        if (kv->term_width < (int)sizeof(sep))
            sep[kv->term_width - 1] = '\0';
        fprintf(output->file, "%s\n", sep);
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);
    }
    // nested data object: break before
    else {
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);
        fprintf(output->file, "\n");
        kv->column = 0;
    }
    
	++kv->data_recursion;
	while (data) {
        // break before some known keys
        if (kv->column > 0 && kv_break_before_key(data->key)) {
            fprintf(output->file, "\n");
            kv->column = 0;
        }
        // break if not enough width left
        else if (kv->column >= kv->term_width - 26) {
            fprintf(output->file, "\n");
            kv->column = 0;
		}
        // pad to next alignment if there is enough width left
        else if (kv->column > 0 && kv->column < kv->term_width - 26) {
            kv->column += fprintf(output->file, "%*s", 25 - kv->column % 26, " ");
        }

		// print key
        char *key = *data->pretty_key ? data->pretty_key : data->key;
        kv->column += fprintf(output->file, "%-10s: ", key);
        // print value
        if (color)
            term_set_fg(kv->term, kv_color_for_key(data->key));
		print_value(output, data->type, data->value, data->format);
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);
        
		// force break after some known keys
        if (kv->column > 0 && kv_break_after_key(data->key)) {
            kv->column = kv->term_width; // force break;
        }

		data = data->next;
    }
    --kv->data_recursion;
    
	// top-level: always end with newline
    if (!kv->data_recursion && kv->column > 0) {
        //fprintf(output->file, "\n"); // data_output_print() already adds a newline
        kv->column = 0;
    }
}

static void print_kv_array(data_output_t *output, data_array_t *array, char *format)
{
	data_output_kv_t *kv = (data_output_kv_t *)output;

	//fprintf(output->file, "[ ");
	for (int c = 0; c < array->num_values; ++c) {
		if (c)
			fprintf(output->file, ", ");
		print_array_value(output, array, format, c);
	}
	//fprintf(output->file, " ]");
}

static void print_kv_double(data_output_t *output, double data, char *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;
    
	kv->column += fprintf(output->file, format ? format : "%.3f", data);
}

static void print_kv_int(data_output_t *output, int data, char *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;
    
	kv->column += fprintf(output->file, format ? format : "%d", data);
}

static void print_kv_string(data_output_t *output, const char *data, char *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;
    
	kv->column += fprintf(output->file, format ? format : "%s", data);
}

static void data_output_kv_free(data_output_t *output)
{
    if (!output)
        return;
    
	if(output->file != stdout)
		fclose(output->file);

	free(output);
}

data_output_t *data_output_kv_create(FILE *file)
{
    data_output_kv_t *kv = calloc(1, sizeof(data_output_kv_t));
    if (!kv) {
	    rtl433_fprintf(stderr, "calloc() failed");
        return NULL;
    }

    kv->output.print_data   = print_kv_data;
	kv->output.print_array  = print_kv_array;
	kv->output.print_string = print_kv_string;
	kv->output.print_double = print_kv_double;
	kv->output.print_int    = print_kv_int;
	kv->output.output_free  = data_output_kv_free;
	kv->output.file         = file;
	kv->output.ext_callback = NULL; // prevents this printer to receive unknown signals

    kv->term = term_init(file);
    kv->color = term_has_color(kv->term);
    
	kv->ring_bell = 0; // TODO: enable if requested...
    
	return &kv->output;
}

