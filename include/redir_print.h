#ifndef RTL_433_REDIR_PRINT_H
#define RTL_433_REDIR_PRINT_H

#define LOG_TRG_STDERR 1
#define LOG_TRG_STDOUT 2

#include <stdio.h>
#include "librtl_433_export.h"

typedef void(*std_print_wrapper)(char target, char *text, void *ctx);

/* Configure a redirection for data printed to stdout or stderr
 * \param cb callback function to receive printed data
 * \param ctx user specific context to pass via the callback function
 * \return 0 on success
 */
RTL_433_API int rtl433_print_redirection(std_print_wrapper cb, void *ctx);

/*
 * fprintf wrapper. If there's a registered redirection, it prints to a buffer
 * and sends it to the callback.
 * otherwise normal output to specified stream.
 */
int rtl433_fprintf(FILE *stream, const char* aFormat, ...);

#endif // RTL_433_REDIR_PRINT_H
