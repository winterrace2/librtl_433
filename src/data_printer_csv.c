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
#include "data_printer_csv.h"

/* CSV printer; doesn't really support recursive data objects yet */

typedef struct {
	data_output_t output;
    const char **fields;
    int data_recursion;
    const char *separator;
} data_output_csv_t;

static void print_csv_data(data_output_t *output, data_t *data, char *format){
    data_output_csv_t *csv = (data_output_csv_t *)output;

    const char **fields = csv->fields;
    int i;

    if (csv->data_recursion)
        return;

    ++csv->data_recursion;
    for (i = 0; fields[i]; ++i) {
        const char *key = fields[i];
        data_t *found = NULL;
        if (i)
            fprintf(output->file, "%s", csv->separator);
        for (data_t *iter = data; !found && iter; iter = iter->next)
            if (strcmp(iter->key, key) == 0)
                found = iter;

        if (found)
            print_value(output, found->type, found->value, found->format);
    }
    --csv->data_recursion;
}

static void print_csv_array(data_output_t *output, data_array_t *array, char *format){
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(output->file, ";");
        print_array_value(output, array, format, c);
    }
}

static void print_csv_string(data_output_t *output, const char *str, char *format){
    data_output_csv_t *csv = (data_output_csv_t *)output;

    while (*str) {
        if (strncmp(str, csv->separator, strlen(csv->separator)) == 0)
            fputc('\\', output->file);
        fputc(*str, output->file);
        ++str;
    }
}

static int compare_strings(const void *a, const void *b){
    return strcmp(*(char **)a, *(char **)b);
}

void data_output_csv_start(data_output_t *output, const char **fields, int num_fields){
	data_output_csv_t *csv = (data_output_csv_t *)output;

    int csv_fields = 0;
    int i, j;
    const char **allowed = NULL;
    int *use_count = NULL;
    int num_unique_fields;
    if (!csv)
        goto alloc_error;

    csv->separator = ",";

    allowed = calloc(num_fields, sizeof(const char *));
	if (!allowed)
		goto alloc_error;
	memcpy(allowed, fields, sizeof(const char *) * num_fields);

    qsort(allowed, num_fields, sizeof(char *), compare_strings);

    // overwrite duplicates
    i = 0;
    j = 0;
    while (j < num_fields) {
        while (j > 0 && j < num_fields &&
                strcmp(allowed[j - 1], allowed[j]) == 0)
            ++j;

        if (j < num_fields) {
            allowed[i] = allowed[j];
            ++i;
            ++j;
        }
    }
    num_unique_fields = i;

    csv->fields = calloc(num_unique_fields + 1, sizeof(const char *));
    if (!csv->fields)
        goto alloc_error;

    use_count = calloc(num_unique_fields, sizeof(*use_count));
    if (!use_count)
        goto alloc_error;

    for (i = 0; i < num_fields; ++i) {
        const char **field = bsearch(&fields[i], allowed, num_unique_fields, sizeof(const char *),
                compare_strings);
        int *field_use_count = use_count + (field - allowed);
        if (field && !*field_use_count) {
            csv->fields[csv_fields] = fields[i];
            ++csv_fields;
            ++*field_use_count;
        }
    }
    csv->fields[csv_fields] = NULL;
    free(allowed);
    free(use_count);

    // Output the CSV header
    for (i = 0; csv->fields[i]; ++i) {
        fprintf(csv->output.file, "%s%s", i > 0 ? csv->separator : "", csv->fields[i]);
    }
    fprintf(csv->output.file, "\n");
    return;

alloc_error:
    free(use_count);
    free(allowed);
    if (csv)
        free(csv->fields);
    free(csv);
}

static void print_csv_double(data_output_t *output, double data, char *format) {
	fprintf(output->file, "%.3f", data);
}

static void print_csv_int(data_output_t *output, int data, char *format) {
	fprintf(output->file, "%d", data);
}

static void data_output_csv_free(data_output_t *output){
    data_output_csv_t *csv = (data_output_csv_t *)output;

	if (output->file != stdout)
		fclose(output->file);

	free(csv->fields);
    free(csv);
}

data_output_t *data_output_csv_create(FILE *file){
    data_output_csv_t *csv = calloc(1, sizeof(data_output_csv_t));
    if (!csv) {
		rtl433_fprintf(stderr, "calloc() failed");
        return NULL;
    }

    csv->output.print_data   = print_csv_data;
    csv->output.print_array  = print_csv_array;
    csv->output.print_string = print_csv_string;
    csv->output.print_double = print_csv_double;
    csv->output.print_int    = print_csv_int;
	csv->output.output_start = data_output_csv_start;
    csv->output.output_free  = data_output_csv_free;
    csv->output.file         = file;
	csv->output.ext_callback = NULL; // prevents this printer to receive unknown signals

	return &csv->output;
}

