/**
 * heavily reduced version of Christian Zuckschwerdts option parsing functions
 *
 * Only following 2 functions are used to parse the rtl_tcp query string.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_OPTPARSE_H_
#define INCLUDE_OPTPARSE_H_

#include <stdint.h>

/// Get the next colon separated arg, NULL otherwise.
char *arg_param(char *arg);

/// Parse parm string to host and port.
/// E.g. ":514", "localhost", "[::1]", "127.0.0.1:514", "[::1]:514"
int hostport_param(char *param, char **host, char **port);

#endif /* INCLUDE_OPTPARSE_H_ */
