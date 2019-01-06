#include <stdbool.h>

#include "librtl_433.h"
#include "data.h"
#include "data_printer_ext.h"
#include "redir_print.h"

/* External callback with extended information */

static void print_extout_array(data_output_t *output, data_array_t *array, char *format){
    rtl433_fprintf(stderr, "Unexpected call of print_extout_array().\n");
}

static void print_extout_data(data_output_t *output, data_t *data, char *format){
    if (output && output->ext_callback) {
        rx_callback cb = (rx_callback)output->ext_callback;
        cb((data_ext_t *)data);
    }
    else rtl433_fprintf(stderr, "call of print_extout_data() with unexpected parameters.\n");
}

static void print_extout_string(data_output_t *output, const char *str, char *format){
    rtl433_fprintf(stderr, "Unexpected call of print_extout_string().\n");
}

static void print_extout_double(data_output_t *output, double data, char *format){
    rtl433_fprintf(stderr, "Unexpected call of print_extout_double().\n");
}

static void print_extout_int(data_output_t *output, int data, char *format){
    rtl433_fprintf(stderr, "Unexpected call of print_extout_int().\n");
}

static void data_output_extout_free(data_output_t *output){
    free(output);
}

data_output_t *data_output_extcb_create(void *cb){
    data_output_t *output = calloc(1, sizeof(data_output_t));
    if (!output) {
        rtl433_fprintf(stderr, "calloc() failed\n");
        return NULL;
    }
    output->print_data   = print_extout_data;
    output->print_array  = print_extout_array;
    output->print_string = print_extout_string;
    output->print_double = print_extout_double;
    output->print_int    = print_extout_int;
    output->output_free  = data_output_extout_free;
    output->file = NULL;
    output->ext_callback = cb; // external callback function
    return output;
}
